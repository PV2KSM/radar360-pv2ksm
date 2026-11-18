// Tela PC: le o servidor web do LibreHardwareMonitor (Windows) pela rede local - http://<pc>:8085/data.json
// e mostra CPU, placa de video, memoria e rede. 3 temas: RGB gamer, grafico ao vivo, gerenciador de tarefas.
#include "Pc.h"
#include "Lang.h"
#include "Gfx.h"
#include "Feat.h"
#include "Http.h"
#include "Json.h"
#include "Dmr.h"
#include "Crash.h"
#include <math.h>
#include <time.h>

struct PcData
{
  char host[32];
  float cpuLoad, cpuTemp, gpuLoad, gpuTemp, gpuClock, memLoad, memUsed, memTotal, netDown, netUp;   // %, °C, MHz, GB, Mb/s
  bool hasGpu;
};

static const int HIST = 31;   // ~1 minuto (de 2 em 2 s)
static PcData cur = {};
static bool have = false;
static float hCpu[HIST], hGpu[HIST], hMem[HIST], hNet[HIST];
static int hCount = 0;
static uint32_t sampleAt = 0;      // millis da ultima amostra (para o grafico deslizar)
static time_t lastSeen = 0;
static char status[64] = "Set the PC address";
static char resolved[72] = "";   // ip:porta encontrado (mostrado no painel)
static bool open_ = false;
static uint32_t pollAt = 0;
static int fails = 0;   // so mostra "desligado" depois de 3 falhas seguidas
static SemaphoreHandle_t mtx = nullptr;
static void lock() { if(mtx) xSemaphoreTake(mtx, portMAX_DELAY); }
static void unlock() { if(mtx) xSemaphoreGive(mtx); }

void pcBegin() { mtx = xSemaphoreCreateMutex(); }
void pcOpen() { open_ = true; pollAt = 0; }
void pcClose() { open_ = false; }

// "58,0 °C", "1.920,0 MHz" (Windows em portugues), "1920.0 MHz", "1.2 MB/s" -> numero (e unidade em u)
static float num(const char *s, const char **unit = nullptr)
{
  char raw[24]; size_t k = 0;
  for(; *s && k + 1 < sizeof(raw); s++)
  {
    if((*s >= '0' && *s <= '9') || *s == '.' || *s == ',' || *s == '-') raw[k++] = *s;
    else if(k) break;
  }
  raw[k] = 0;
  while(*s == ' ') s++;
  if(unit) *unit = s;
  if(!k) return NAN;
  // separador decimal: o ultimo entre '.' e ','; se so houver pontos repetidos, sao de milhar
  const char *lastDot = strrchr(raw, '.'), *lastComma = strrchr(raw, ',');
  int dots = 0; for(const char *c = raw; *c; c++) dots += *c == '.';
  char dec = 0;
  if(lastDot && lastComma) dec = lastDot > lastComma ? '.' : ',';
  else if(lastComma) dec = ',';
  else if(lastDot && dots == 1) dec = '.';
  char b[24]; size_t j = 0;
  for(const char *c = raw; *c; c++)
  {
    if(*c == dec) b[j++] = '.';
    else if(*c != '.' && *c != ',') b[j++] = *c;
  }
  b[j] = 0;
  const float v = strtof(b, nullptr);
  return isfinite(v) ? v : NAN;
}

static float toMbps(float v, const char *u)
{
  if(isnan(v)) return 0;
  if(!strncasecmp(u, "GB", 2)) return v * 8000;
  if(!strncasecmp(u, "MB", 2)) return v * 8;
  if(!strncasecmp(u, "KB", 2)) return v * 8 / 1000;
  return v * 8 / 1e6f;
}

struct Pick { float v; int score; };
static void pick(Pick &p, float v, int score) { if(!isnan(v) && score > p.score) { p.v = v; p.score = score; } }

struct Acc
{
  Pick cpuLoad{NAN, -1}, cpuTemp{NAN, -1}, gpuLoad{NAN, -1}, gpuTemp{NAN, -1}, gpuClock{NAN, -1}, memLoad{NAN, -1}, memUsed{NAN, -1}, memAvail{NAN, -1};
  float down = 0, up = 0;
  bool gpu = false;
};

static void visit(JsonObjectConst n, Acc &a)
{
  const char *id = n["SensorId"] | "";
  if(id[0])
  {
    const char *type = n["Type"] | "", *text = n["Text"] | "";
    const char *unit = ""; const float v = num(n["Value"] | "", &unit);
    const bool cpu = strstr(id, "cpu/") != nullptr, gpu = strstr(id, "/gpu") != nullptr;
    const bool ram = !strncmp(id, "/ram", 4) || strstr(id, "/memory/") != nullptr;
    const bool nic = !strncmp(id, "/nic", 4);
    if(gpu) a.gpu = true;
    if(cpu && !strcmp(type, "Load") && !strcmp(text, "CPU Total")) pick(a.cpuLoad, v, 9);
    if(cpu && !strcmp(type, "Temperature"))
      pick(a.cpuTemp, v, strstr(text, "Package") || strstr(text, "Tctl") || strstr(text, "Tdie") ? 5 : strstr(text, "Average") ? 4 : strstr(text, "Max") ? 3 : 1);
    if(gpu && !strcmp(type, "Load")) pick(a.gpuLoad, v, !strcmp(text, "GPU Core") ? 5 : strstr(text, "3D") ? 3 : 0);
    if(gpu && !strcmp(type, "Temperature")) pick(a.gpuTemp, v, !strcmp(text, "GPU Core") ? 5 : strstr(text, "Hot Spot") ? 2 : 1);
    if(gpu && !strcmp(type, "Clock") && !strcmp(text, "GPU Core")) pick(a.gpuClock, v, 5);
    if(ram && !strstr(text, "Virtual"))
    {
      if(!strcmp(type, "Load")) pick(a.memLoad, v, 5);
      if(!strcmp(type, "Data") && strstr(text, "Used")) pick(a.memUsed, v, 5);
      if(!strcmp(type, "Data") && strstr(text, "Available")) pick(a.memAvail, v, 5);
    }
    if(nic && !strcmp(type, "Throughput"))
    {
      if(strstr(text, "Download")) a.down += toMbps(v, unit);
      if(strstr(text, "Upload")) a.up += toMbps(v, unit);
    }
  }
  for(JsonObjectConst c : n["Children"].as<JsonArrayConst>()) visit(c, a);
}

static void setStatus(const char *s) { lock(); strlcpy(status, s, sizeof(status)); unlock(); }

static void poll()
{
  if(!feat.pcHost[0]) { setStatus(T("Set the PC address","Configure o endereço do PC","Configura la dirección del PC")); return; }
  char host[72];
  char withPort[72]; strlcpy(withPort, feat.pcHost, sizeof(withPort));
  if(!strchr(withPort, ':')) strlcat(withPort, ":8085", sizeof(withPort));
  if(!netResolveHost(withPort, host, sizeof(host)))
  {
    lock(); if(++fails >= 3) have = false; resolved[0] = 0;
    strlcpy(status, T("PC not found by name: use the IP or set the network to Private in Windows","PC não encontrado pelo nome: use o IP ou libere a rede como Privada no Windows","PC no encontrado por nombre: usa la IP o pon la red como Privada en Windows"), sizeof(status)); unlock();
    return;
  }
  lock(); strlcpy(resolved, host, sizeof(resolved)); unlock();
  char url[112]; snprintf(url, sizeof(url), "http://%s/data.json", host);
  HttpBuf buf; char err[40];
  crumbWorker("pc:baixar");
  const int code = httpGet(url, buf, 900 * 1024, err, sizeof(err), "application/json", false, 4000);
  if(code != 200)
  {
    lock(); if(++fails >= 3) have = false; snprintf(status, sizeof(status), code == 401 ? T("The PC asked for a password: disable it in LibreHardwareMonitor","O PC pediu senha: desligue a senha no LibreHardwareMonitor","El PC pidió contraseña: desactívala en LibreHardwareMonitor") : "PC: %s", err); unlock();
    return;
  }
  crumbWorker("pc:json");
  // Guarda so os campos usados (sem Min, Max, ImageURL, id): o documento fica varias vezes menor
  static JsonDocument filter;
  if(filter.isNull())
  {
    JsonObject f = filter.to<JsonObject>();
    for(int depth = 0; depth < 12; depth++)
    {
      f["Text"] = true; f["SensorId"] = true; f["Type"] = true; f["Value"] = true;
      f = f["Children"].add<JsonObject>();
    }
  }
  JsonDocument doc(SpiRamAllocator::instance());
  const DeserializationError e = deserializeJson(doc, buf.str(), buf.len, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(40));
  buf.release();
  if(e) { setStatus(e == DeserializationError::NoMemory ? T("PC: out of memory to read sensors","PC: sem memória para ler os sensores","PC: sin memoria para leer sensores") : "PC: resposta inválida"); return; }
  Acc a;
  visit(doc.as<JsonObjectConst>(), a);
  PcData d = {};
  const char *name = doc["Children"][0]["Text"] | "PC";
  strlcpy(d.host, name, sizeof(d.host));
  d.cpuLoad = isnan(a.cpuLoad.v) ? 0 : a.cpuLoad.v;
  d.cpuTemp = a.cpuTemp.v;
  d.hasGpu = a.gpu;
  d.gpuLoad = isnan(a.gpuLoad.v) ? 0 : a.gpuLoad.v;
  d.gpuTemp = a.gpuTemp.v;
  d.gpuClock = a.gpuClock.v;
  d.memUsed = a.memUsed.v;
  d.memTotal = (!isnan(a.memUsed.v) && !isnan(a.memAvail.v)) ? a.memUsed.v + a.memAvail.v : NAN;
  d.memLoad = !isnan(a.memLoad.v) ? a.memLoad.v : (!isnan(d.memTotal) && d.memTotal > 0 ? 100 * d.memUsed / d.memTotal : 0);
  d.netDown = constrain(a.down, 0.0f, 100000.0f); d.netUp = constrain(a.up, 0.0f, 100000.0f);
  lock();
  cur = d; have = true; fails = 0; status[0] = 0; lastSeen = time(nullptr);
  auto push = [](float *h, float v) { memmove(h, h + 1, sizeof(float) * (HIST - 1)); h[HIST - 1] = v; };
  push(hCpu, d.cpuLoad); push(hGpu, d.gpuLoad); push(hMem, d.memLoad); push(hNet, d.netDown);
  if(hCount < HIST) hCount++;
  sampleAt = millis();
  unlock();
  crumbWorker("pc:ok");
}

void pcWork()
{
  if(!feat.scrPc) return;
  if((int32_t)(millis() - pollAt) < 0) return;
  const uint32_t every = feat.pcSecs * 1000UL;
  pollAt = millis() + (open_ ? every : max(every, 10000UL));   // fora da tela: devagar, so para o grafico ter historia
  poll();
}

const char *pcStatus() { return status; }
const char *pcResolved() { return resolved; }
bool pcAnimated() { return true; }

// ---------------- Desenho ----------------
static uint32_t mixc(uint32_t a, uint32_t b, float t)
{
  t = constrain(t, 0.0f, 1.0f);
  const int r = ((a >> 16) & 255) + (((int)((b >> 16) & 255) - (int)((a >> 16) & 255)) * t);
  const int g = ((a >> 8) & 255) + (((int)((b >> 8) & 255) - (int)((a >> 8) & 255)) * t);
  const int bl = (a & 255) + (((int)(b & 255) - (int)(a & 255)) * t);
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

static uint32_t hsv(float h, float s, float v)
{
  h = fmodf(h, 360.0f); if(h < 0) h += 360;
  const float c = v * s, x = c * (1 - fabsf(fmodf(h / 60.0f, 2) - 1)), m = v - c;
  float r, g, b;
  if(h < 60) { r = c; g = x; b = 0; } else if(h < 120) { r = x; g = c; b = 0; } else if(h < 180) { r = 0; g = c; b = x; }
  else if(h < 240) { r = 0; g = x; b = c; } else if(h < 300) { r = x; g = 0; b = c; } else { r = c; g = 0; b = x; }
  return ((uint32_t)((r + m) * 255) << 16) | ((uint32_t)((g + m) * 255) << 8) | (uint32_t)((b + m) * 255);
}

// verde tranquilo -> laranja quente -> vermelho
static uint32_t heat(float t)
{
  if(isnan(t)) return 0x8FA3B8u;
  if(t < 60) return mixc(0x47E6B5u, 0xFFC94Fu, (t - 40) / 20.0f);
  return mixc(0xFFC94Fu, 0xFF4A3Du, (t - 60) / 25.0f);
}

static void tempStr(char *b, size_t n, float t) { if(isnan(t)) strlcpy(b, "--°", n); else snprintf(b, n, "%d°", (int)lroundf(t)); }

static void offline(const char *st, time_t seen)
{
  gfxGradientV(0x0E141Cu, 0x0A1018u, 0x070B11u);
  const int cx = CX, cy = 140;
  gfxArc(cx, cy, 30, 34, 35, 325, 0x3A4656u, 255);
  canvas.fillSmoothRoundRect(cx - 2, cy - 40, 5, 36, 2, 0x3A4656u);
  gfxText(feat.pcHost[0] ? T("PC off","PC desligado","PC apagado") : "PC", CX, 210, FontTitle, 0x9AABBCu);
  if(seen)
  {
    struct tm t; localtime_r(&seen, &t);
    char b[48]; snprintf(b, sizeof(b), T("last seen at %02d:%02d","visto pela última vez às %02d:%02d","visto por última vez a las %02d:%02d"), t.tm_hour, t.tm_min);
    gfxText(b, CX, 242, FontSmall, 0x667788u);
  }
  if(st[0]) gfxWrapRoundCentered(st, 286, FxInter13, 0x8899AAu, 18, 3, 50);
}

// 6. RGB gamer: anel arco-iris girando (mais rapido com a GPU carregada)
static void drawRgb(const PcData &d)
{
  canvas.fillScreen(0x07070Du);
  uint16_t *buf = (uint16_t *)canvas.getBuffer();
  const float load = (d.hasGpu ? d.gpuLoad : d.cpuLoad) / 100.0f;
  const float rot = fmodf(millis() / 1000.0f * (40 + 220 * load), 360.0f);
  const float R0 = 154, R1 = 172;
  for(int y = 0; y < LCD_H; y++)
  {
    const float dy = y + 0.5f - CY;
    if(fabsf(dy) > R1 + 1) continue;
    const float xo = sqrtf(max(0.0f, (R1 + 1) * (R1 + 1) - dy * dy));
    const float xi2 = (R0 - 1) * (R0 - 1) - dy * dy;
    const float xi = xi2 > 0 ? sqrtf(xi2) : -1;
    // so as duas faixas do anel nesta linha (pula o miolo; o laco sempre avanca)
    const int xa = max(0, (int)(CX - xo)), xb = min(LCD_W - 1, (int)(CX + xo));
    const int skipA = xi > 0 ? (int)ceilf(CX - xi) : xb + 1, skipB = xi > 0 ? (int)floorf(CX + xi) - 1 : xb;
    for(int x = xa; x <= xb; x++)
    {
      if(x >= skipA + 1 && x <= skipB) { x = skipB; continue; }
      const float dx = x + 0.5f - CX;
      const float r = sqrtf(dx * dx + dy * dy);
      const float cov = constrain(min(r - R0 + 0.5f, R1 - r + 0.5f), 0.0f, 1.0f);
      if(cov <= 0) continue;
      const float ang = atan2f(dx, -dy) * RAD_TO_DEG;
      // brilho mais forte no meio do anel e pequenas falhas entre os "LEDs"
      const float seg = fmodf(ang + 360, 10.0f);
      const float led = seg < 1.2f ? 0.35f : 1.0f;
      const float mid = 1 - fabsf((r - (R0 + R1) / 2) / ((R1 - R0) / 2)) * 0.45f;
      uint16_t &p = buf[y * LCD_W + x];
      p = gfxBlend(p, hsv(ang + rot, 0.85f, 1.0f * mid * led), (uint8_t)(cov * 255));
    }
  }
  // brilho interno do anel
  gfxGlow(CX, CY, 150, hsv(rot + 200, 0.8f, 1), 40);
  char b[48];
  const bool g = d.hasGpu;
  gfxTextSpaced(g ? "GPU" : "CPU", CX, 96, FxOsw14, 0xC39BFFu, 4);
  tempStr(b, sizeof(b), g ? d.gpuTemp : d.cpuTemp);
  gfxText(b, CX + 8, 160, FxClockL, 0xFFFFFFu);
  if(g && !isnan(d.gpuClock)) snprintf(b, sizeof(b), "%d%% · %d MHz", (int)lroundf(d.gpuLoad), (int)lroundf(d.gpuClock));
  else snprintf(b, sizeof(b), T("%d%% used","%d%% de uso","%d%% de uso"), (int)lroundf(g ? d.gpuLoad : d.cpuLoad));
  gfxText(b, CX, 214, FontSmall, 0xFF5AD8u);
  if(g)
  {
    char t[8]; tempStr(t, sizeof(t), d.cpuTemp);
    snprintf(b, sizeof(b), "CPU %s · %d%%", t, (int)lroundf(d.cpuLoad));
    gfxText(b, CX, 244, FontSmall, 0x5AD8FFu);
  }
  snprintf(b, sizeof(b), "RAM %d%%", (int)lroundf(d.memLoad));
  gfxText(b, CX, 270, FxInter13, 0x9A9AC8u);
  gfxTextFit(d.host, CX, 66, 180, FxInter13, 0x77779Au);
}

// linha suave do historico; o grafico desliza entre as amostras
static void history(const float *h, int count, int x0, int x1, int y0, int y1, float maxV, uint32_t col, uint8_t fillA, float slide)
{
  if(count < 2) return;
  uint16_t *buf = (uint16_t *)canvas.getBuffer();
  const float step = (float)(x1 - x0) / (HIST - 2);
  auto X = [&](int i) { return x1 - (HIST - 1 - i - slide) * step; };   // i = indice no vetor
  auto Y = [&](float v) { return y1 - constrain(v / maxV, 0.0f, 1.0f) * (y1 - y0); };
  const int first = HIST - count;
  // area
  if(fillA)
    for(int i = max(first, 1); i < HIST; i++)
    {
      const float xa = X(i - 1), xb = X(i);
      for(int x = max(x0, (int)ceilf(xa)); x <= min(x1, (int)xb); x++)
      {
        const float f = (x - xa) / (xb - xa);
        const float y = Y(h[i - 1] + (h[i] - h[i - 1]) * f);
        for(int yy = (int)y; yy < y1; yy++)
        {
          uint16_t &p = buf[yy * LCD_W + x];
          p = gfxBlend(p, col, (uint8_t)(fillA * (1 - (yy - y) / (y1 - y + 1) * 0.7f)));
        }
      }
    }
  // linha
  for(int i = max(first, 1); i < HIST; i++)
  {
    float xa = X(i - 1), xb = X(i), ya = Y(h[i - 1]), yb = Y(h[i]);
    if(xb < x0) continue;
    if(xa < x0) { const float f = (x0 - xa) / (xb - xa); ya += (yb - ya) * f; xa = x0; }
    canvas.drawWideLine(xa, ya, min((float)x1, xb), yb, 1.4f, col);
  }
}

static float slideNow()
{
  const float s = (millis() - sampleAt) / (feat.pcSecs * 1000.0f);
  return 1 - constrain(s, 0.0f, 1.0f);   // a amostra nova entra pela direita
}

// 8. Grafico ao vivo: CPU e GPU no ultimo minuto
static void drawGraph(const PcData &d, const float *hc, const float *hg, int count)
{
  gfxGradientV(0x0E1622u, 0x0A1018u, 0x070B11u);
  {
    const int span = (HIST - 1) * feat.pcSecs;   // quanto tempo o grafico cobre
    char t[32];
    if(span < 120) snprintf(t, sizeof(t), "ÚLTIMOS %d s", span); else snprintf(t, sizeof(t), "ÚLTIMOS %d MIN", span / 60);
    gfxTextSpaced(t, CX, 54, FxOsw14, 0x8FA3B8u, 3);
  }
  const int x0 = 34, x1 = 326, y0 = 92, y1 = 236;
  for(int k = 0; k <= 4; k++)
  {
    const int y = y0 + k * (y1 - y0) / 4;
    canvas.drawFastHLine(x0, y, x1 - x0, 0x1A2430u);
  }
  gfxText("100%", x0 + 2, y0 - 8, FxInter13, 0x455566u, middle_left);
  const float sl = slideNow();
  if(d.hasGpu) history(hg, count, x0, x1, y0, y1, 100, 0xFF8A3Du, 40, sl);
  history(hc, count, x0, x1, y0, y1, 100, 0x47E6B5u, 50, sl);
  char b[40], t[8];
  canvas.fillSmoothCircle(70, 268, 5, 0x47E6B5u);
  tempStr(t, sizeof(t), d.cpuTemp);
  snprintf(b, sizeof(b), "CPU %d%% · %s", (int)lroundf(d.cpuLoad), t);
  gfxText(b, 82, 268, FontSmall, 0xFFFFFFu, middle_left);
  if(d.hasGpu)
  {
    canvas.fillSmoothCircle(70, 294, 5, 0xFF8A3Du);
    tempStr(t, sizeof(t), d.gpuTemp);
    snprintf(b, sizeof(b), "GPU %d%% · %s", (int)lroundf(d.gpuLoad), t);
    gfxText(b, 82, 294, FontSmall, 0xFFFFFFu, middle_left);
  }
  snprintf(b, sizeof(b), "RAM %d%%  ·  ↓ %.0f Mb/s", (int)lroundf(d.memLoad), d.netDown);
  gfxTextFit(b, CX, 320, 200, FxInter13, 0x8FA3B8u);
}

// 12. Gerenciador de Tarefas: 4 cartoes com grafico de area
static void card(int x, int y, int w, int h, const char *label, const char *value, uint32_t col, const float *hist, int count, float maxV, float sl)
{
  canvas.fillSmoothRoundRect(x, y, w, h, 10, 0x25282Fu);
  gfxText(label, x + 10, y + 14, FxInter13, 0xD8D8DCu, middle_left);
  gfxText(value, x + w - 10, y + 14, FxInter13, col, middle_right);
  history(hist, count, x + 8, x + w - 8, y + 28, y + h - 8, maxV, col, 90, sl);
}

static void drawTasks(const PcData &d, const float *hc, const float *hg, const float *hm, const float *hn, int count)
{
  canvas.fillScreen(0x1B1D22u);
  gfxText(T("Performance","Desempenho","Rendimiento"), CX, 46, FontSmall, 0xE8E8ECu);
  const float sl = slideNow();
  char b[24];
  const int W = 118, H = 96, G = 10, X0 = CX - W - G / 2, Y0 = 70;
  snprintf(b, sizeof(b), "%d%%", (int)lroundf(d.cpuLoad));
  card(X0, Y0, W, H, "CPU", b, 0x47A8FFu, hc, count, 100, sl);
  if(d.hasGpu) { snprintf(b, sizeof(b), "%d%%", (int)lroundf(d.gpuLoad)); card(X0 + W + G, Y0, W, H, "GPU", b, 0xC39BFFu, hg, count, 100, sl); }
  else card(X0 + W + G, Y0, W, H, "GPU", "—", 0x667788u, hg, 0, 100, sl);
  if(!isnan(d.memUsed)) snprintf(b, sizeof(b), "%.1f GB", d.memUsed); else snprintf(b, sizeof(b), "%d%%", (int)lroundf(d.memLoad));
  card(X0, Y0 + H + G, W, H, "RAM", b, 0xFF8ACBu, hm, count, 100, sl);
  float nmax = 1; for(int i = HIST - count; i < HIST; i++) nmax = max(nmax, hn[i]);
  snprintf(b, sizeof(b), d.netDown >= 10 ? "%.0f Mb/s" : "%.1f Mb/s", d.netDown);
  card(X0 + W + G, Y0 + H + G, W, H, T("Network","Rede","Red"), b, 0xFFCC4Du, hn, count, nmax * 1.15f, sl);
  char t1[8], t2[8]; tempStr(t1, sizeof(t1), d.cpuTemp); tempStr(t2, sizeof(t2), d.gpuTemp);
  if(d.hasGpu) snprintf(b, sizeof(b), "CPU %s · GPU %s", t1, t2); else snprintf(b, sizeof(b), "CPU %s", t1);
  gfxText(b, CX, 296, FontSmall, 0xB8BCC6u);
  gfxTextFit(d.host, CX, 320, 160, FxInter13, 0x6A6E78u);
}

void pcDraw()
{
  lock();
  const bool ok = have; const PcData d = cur; char st[64]; strlcpy(st, status, sizeof(st));
  float hc[HIST], hg[HIST], hm[HIST], hn[HIST];
  memcpy(hc, hCpu, sizeof(hc)); memcpy(hg, hGpu, sizeof(hg)); memcpy(hm, hMem, sizeof(hm)); memcpy(hn, hNet, sizeof(hn));
  const int count = hCount; const time_t seen = lastSeen;
  unlock();
  if(!ok) { offline(st, seen); return; }
  switch(feat.pcStyle)
  {
    case 1: drawGraph(d, hc, hg, count); break;
    case 2: drawTasks(d, hc, hg, hm, hn, count); break;
    default: drawRgb(d); break;
  }
}
