#include "Dmr.h"
#include "Lang.h"
#include "Gfx.h"
#include "Feat.h"
#include "Http.h"
#include "Net.h"
#include "Ui.h"
#include "Crash.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include "NetName.h"
#include <ArduinoJson.h>
#include <time.h>
#include "Flags.h"
#include <esp_heap_caps.h>

#define MAX_ROWS 10
#define MAX_LOOKUPS 40

struct Row { char call[12]; uint32_t tg; int dur; char when[6]; bool live; char slot[4]; char src[8]; };
struct Who { char call[12]; char name[28]; char place[40]; uint32_t id; bool done; char cc[3]; char country[28]; };

static SemaphoreHandle_t mtx;
static Row rows[MAX_ROWS];
static int rowCount = 0;
static Who who[MAX_LOOKUPS];
static int whoNext = 0;
static char status[64] = "Procurando o hotspot…";
static bool ok = false;
static uint32_t pollAt = 0;
static bool listView = false;
static uint32_t liveSince = 0;
static char liveCall[12] = "";

static void lock() { xSemaphoreTake(mtx, portMAX_DELAY); }
static void unlock() { xSemaphoreGive(mtx); }

void dmrBegin() { mtx = xSemaphoreCreateMutex(); }
const char *dmrStatus() { return status; }
void dmrOpen() { pollAt = 0; }
bool dmrAnimated() { return rowCount && rows[0].live; }   // cronometro do QSO ao vivo
void dmrLongPress() { listView = !listView; }

static void setStatus(const char *s) { lock(); strlcpy(status, s, sizeof(status)); unlock(); }

// Endereco: IP, nome DNS ou nome.local (mDNS). Guarda os 2 ultimos (hotspot e PC).
bool netResolveHost(const char *host, char *out, size_t n)
{
  struct C { char h[64]; char o[72]; uint32_t t; };
  static C cache[2] = {};
  for(auto &c : cache) if(!strcmp(c.h, host) && c.o[0] && millis() - c.t < 120000) { strlcpy(out, c.o, n); return true; }
  // nome que nao foi achado ha pouco: nao repete a busca (que demora alguns segundos) a cada consulta
  static char failH[64] = ""; static uint32_t failT = 0;
  if(!strcmp(failH, host) && millis() - failT < 30000) return false;
  char name[64]; strlcpy(name, host, sizeof(name)); char port[8] = "";
  char *colon = strchr(name, ':'); if(colon) { strlcpy(port, colon, sizeof(port)); *colon = 0; }
  IPAddress ip; bool found = ip.fromString(name);
  // nome simples (ex.: DESKTOP-PC): Windows responde por NetBIOS/LLMNR; depois tenta nome.local e o DNS do roteador
  if(!found && !strchr(name, '.'))
  {
    found = netLocalNameLookup(name, ip);
    if(!found) { char l[72]; snprintf(l, sizeof(l), "%s", name); ip = MDNS.queryHost(l, 1500); found = ip != IPAddress((uint32_t)0); }
    for(const char *suf : {"", ".lan", ".home", ".local"})
    {
      if(found) break;
      char fq[72]; snprintf(fq, sizeof(fq), "%s%s", name, suf);
      found = WiFi.hostByName(fq, ip) == 1 && ip != IPAddress((uint32_t)0);
    }
  }
  if(!found)
  {
    const size_t L = strlen(name); const bool local = L > 6 && !strcasecmp(name + L - 6, ".local");
    char bare[64]; strlcpy(bare, name, sizeof(bare)); if(local) bare[L - 6] = 0;
    if(local) { ip = MDNS.queryHost(bare, 2000); found = ip != IPAddress((uint32_t)0); }
    if(!found) found = WiFi.hostByName(name, ip) == 1;
    if(!found && !local) { ip = MDNS.queryHost(bare, 2000); found = ip != IPAddress((uint32_t)0); }
  }
  if(!found) { strlcpy(failH, host, sizeof(failH)); failT = millis(); return false; }
  if(!strcmp(failH, host)) failH[0] = 0;
  snprintf(out, n, "%s%s", ip.toString().c_str(), port);
  C &v = cache[0].t <= cache[1].t ? cache[0] : cache[1];
  strlcpy(v.h, host, sizeof(v.h)); strlcpy(v.o, out, sizeof(v.o)); v.t = millis();
  return true;
}
static bool resolveHost(const char *host, char *out, size_t n) { return netResolveHost(host, out, n); }

static void stripCell(const char *a, const char *b, char *out, size_t n)
{
  size_t o = 0; bool tag = false;
  for(const char *p = a; p < b && o + 1 < n; p++)
  {
    if(*p == '<') { tag = true; continue; }
    if(*p == '>') { tag = false; continue; }
    if(tag) continue;
    if(*p == '&') { const char *s = strchr(p, ';'); if(s && s < b && s - p < 8) { out[o++] = ' '; p = s; continue; } }
    out[o++] = (*p == '\n' || *p == '\r' || *p == '\t') ? ' ' : *p;
  }
  while(o && out[o - 1] == ' ') o--;
  out[o] = 0;
  size_t s = 0; while(out[s] == ' ') s++;
  if(s) memmove(out, out + s, o - s + 1);
}

static void pollHotspot()
{
  if(!feat.dHost[0]) { setStatus(T("Set the hotspot address","Configure o endereço do hotspot","Configura la dirección del hotspot")); return; }
  char addr[48];
  if(!resolveHost(feat.dHost, addr, sizeof(addr))) { ok = false; setStatus(T("Hotspot not found","Hotspot não encontrado","Hotspot no encontrado")); return; }
  HttpBuf buf; char err[40];
  int code = -1;
  for(const char *path : {"/mmdvmhost/lh.php", "/"})
  {
    char url[96]; snprintf(url, sizeof(url), "http://%s%s", addr, path);
    code = httpGet(url, buf, 96 * 1024, err, sizeof(err), "text/html", true, 5000);
    if(code == 200) break;
  }
  if(code != 200) { ok = false; char m[64]; snprintf(m, sizeof(m), "Hotspot: %s", err); setStatus(m); return; }

  // Tabela Last Heard: Hora | Modo | Indicativo | Destino | Origem | Dur | Perda | BER
  Row nr[MAX_ROWS]; int n = 0;
  const char *p = buf.str();
  while(n < MAX_ROWS && (p = strstr(p, "<tr")))
  {
    const char *end = strstr(p, "</tr>"); if(!end) break;
    char cell[9][48]; int nc = 0; const char *q = p;
    while(nc < 9 && (q = strstr(q, "<td")) && q < end)
    {
      const char *gt = strchr(q, '>'); if(!gt || gt >= end) break;
      const char *te = strstr(gt, "</td>"); if(!te || te > end) te = end;
      stripCell(gt + 1, te, cell[nc], sizeof(cell[nc])); nc++; q = te;
    }
    p = end + 5;
    int mi = -1; for(int i = 0; i < nc; i++) if(strstr(cell[i], "DMR")) { mi = i; break; }
    if(mi < 0 || mi + 2 >= nc) continue;
    Row &r = nr[n]; memset(&r, 0, sizeof(r));
    strlcpy(r.call, cell[mi + 1], sizeof(r.call)); char *sl = strpbrk(r.call, " /"); if(sl) *sl = 0;
    const char *ts = strstr(cell[mi], "TS"); if(ts) strlcpy(r.slot, ts, sizeof(r.slot));
    const char *tgp = cell[mi + 2]; while(*tgp && (*tgp < '0' || *tgp > '9')) tgp++; r.tg = (uint32_t)atol(tgp);
    if(mi + 3 < nc) strlcpy(r.src, cell[mi + 3], sizeof(r.src));
    for(int i = mi + 3; i < nc; i++)
    {
      if(!strncmp(cell[i], "TX", 2)) { r.live = true; break; }
      if(cell[i][0] >= '0' && cell[i][0] <= '9' && strchr(cell[i], '.')) { r.dur = (int)atof(cell[i]); break; }
    }
    // Hora: pega HH:MM da primeira coluna
    const char *c = strchr(cell[0], ':'); if(c && c - cell[0] >= 2) { strncpy(r.when, c - 2, 5); r.when[5] = 0; }
    if(r.call[0]) n++;
  }
  buf.release();
  lock();
  memcpy(rows, nr, sizeof(Row) * n); rowCount = n; ok = true;
  if(n && rows[0].live) { if(strcmp(liveCall, rows[0].call)) { strlcpy(liveCall, rows[0].call, sizeof(liveCall)); liveSince = millis(); } }
  else liveCall[0] = 0;
  status[0] = 0;
  if(!n) strlcpy(status, T("No station heard yet","Nenhuma estação ouvida ainda","Ninguna estación escuchada aún"), sizeof(status));
  unlock();
}

static Who *findWho(const char *call)
{
  for(auto &w : who) if(w.call[0] && !strcmp(w.call, call)) return &w;
  return nullptr;
}

// "Sao Paulo" -> "SP" (o RadioID manda o nome do estado por extenso)
static const char *ufOf(const char *state)
{
  static const char *const T[][2] = {
    {"Acre","AC"},{"Alagoas","AL"},{"Amapá","AP"},{"Amazonas","AM"},{"Bahia","BA"},{"Ceará","CE"},{"Distrito Federal","DF"},
    {"Espírito Santo","ES"},{"Goiás","GO"},{"Maranhão","MA"},{"Mato Grosso","MT"},{"Mato Grosso do Sul","MS"},{"Minas Gerais","MG"},
    {"Pará","PA"},{"Paraíba","PB"},{"Paraná","PR"},{"Pernambuco","PE"},{"Piauí","PI"},{"Rio de Janeiro","RJ"},
    {"Rio Grande do Norte","RN"},{"Rio Grande do Sul","RS"},{"Rondônia","RO"},{"Roraima","RR"},{"Santa Catarina","SC"},
    {"São Paulo","SP"},{"Sao Paulo","SP"},{"Sergipe","SE"},{"Tocantins","TO"},{"Espirito Santo","ES"},{"Goias","GO"},
    {"Maranhao","MA"},{"Para","PA"},{"Paraiba","PB"},{"Parana","PR"},{"Piaui","PI"},{"Rondonia","RO"},{"Ceara","CE"},{"Amapa","AP"}};
  for(const auto &e : T) if(!strcasecmp(state, e[0])) return e[1];
  return state;
}

static void lookupOne()
{
  if(!feat.dLookup) return;
  char call[12] = "";
  lock();
  for(int i = 0; i < rowCount && !call[0]; i++) if(!findWho(rows[i].call)) strlcpy(call, rows[i].call, sizeof(call));
  unlock();
  if(!call[0]) return;
  Who w; memset(&w, 0, sizeof(w)); strlcpy(w.call, call, sizeof(w.call)); w.done = true;
  char url[96]; snprintf(url, sizeof(url), "https://radioid.net/api/dmr/user/?callsign=%s", call);
  HttpBuf buf; char err[32];
  if(httpGet(url, buf, 24 * 1024, err, sizeof(err), "application/json", false, 8000) == 200)
  {
    JsonDocument doc;
    if(!deserializeJson(doc, buf.str(), buf.len))
    {
      JsonObjectConst r = doc["results"][0];
      if(!r.isNull())
      {
        char fname[32]; strlcpy(fname, r["fname"] | "", sizeof(fname));
        char *sp = strchr(fname, ' '); if(sp) *sp = 0;
        strlcpy(w.name, fname, sizeof(w.name));
        const char *city = r["city"] | "", *state = r["state"] | "", *country = r["country"] | "";
        if(city[0] && state[0]) snprintf(w.place, sizeof(w.place), "%s, %s", city, ufOf(state));
        else strlcpy(w.place, city[0] ? city : country, sizeof(w.place));
        w.id = r["id"] | 0;
        flagCountryByName(country, w.cc, w.country, sizeof(w.country));
      }
    }
  }
  buf.release();
  if(!w.cc[0] && flagCountryByCall(call, w.cc)) flagCountryByCode(w.cc, w.country, sizeof(w.country));
  lock(); who[whoNext] = w; whoNext = (whoNext + 1) % MAX_LOOKUPS; unlock();
}

void dmrWork()
{
  if(!feat.scrDmr || uiCurrent() != SCR_DMR || !netConnected()) return;
  if(!pollAt || (int32_t)(millis() - pollAt) >= 0)
  {
    pollAt = millis() + feat.dPollSecs * 1000UL;
    crumbWorker("dmr:hotspot");
    pollHotspot();
    crumbWorker("dmr:radioid");
    lookupOne();
  }
}

static void tgText(char *b, size_t n, uint32_t tg)
{
  if(tg == 4000) snprintf(b, n, T("Disconnect","Desconectar","Desconectar"));
  else if(tg == 9990) snprintf(b, n, "Eco (9990)");
  else snprintf(b, n, "TG %lu", (unsigned long)tg);
}

// ---- Tema Aurora ----
static const uint32_t GOLD = 0xF2C66Du, LAV = 0xB9B4DEu;

static void auroraBg()
{
  if(!gfxImage("bg_aurora")) gfxGradientV(0x03040Eu, 0x0A0C2Au, 0x141238u);
  gfxArc(CX, CY, 168, 169.4f, 0, 359.99f, GOLD, 90);
}

// Ramo de louro: caule curvo com folhas aos pares, menores perto da ponta
static void laurel(bool left)
{
  const float R = 124, a0 = left ? 196 : 164, a1 = left ? 290 : 70;   // graus (0 = topo, horario)
  const float sgn = left ? 1 : -1;
  if(left) gfxArc(CX, CY, R - 0.8f, R + 0.8f, a0, a1, 0xC9A55Au, 230);
  else gfxArc(CX, CY, R - 0.8f, R + 0.8f, a1, a0, 0xC9A55Au, 230);
  const int n = 8;
  for(int k = 0; k < n; k++)
  {
    const float t = (k + 0.5f) / n;
    const float ang = a0 + (a1 - a0) * t;
    const float rad = (ang - 90) * DEG_TO_RAD;
    const float px = CX + cosf(rad) * R, py = CY + sinf(rad) * R;
    // direcao do caule (para a ponta) e normal (para fora)
    const float tx = -sinf(rad) * sgn, ty = cosf(rad) * sgn;
    const float nx = cosf(rad), ny = sinf(rad);
    const float len = 17 - 7 * t, wid = 3.6f - 1.2f * t;
    for(int side = -1; side <= 1; side += 2)
    {
      // folha inclinada 40 graus entre o caule e o lado
      const float dx = tx * 0.77f + nx * 0.64f * side, dy = ty * 0.77f + ny * 0.64f * side;
      const int bx = px, by = py, mx = px + dx * len * 0.5f, my = py + dy * len * 0.5f, ex = px + dx * len, ey = py + dy * len;
      canvas.drawWedgeLine(bx, by, mx, my, 0.5f, wid, side > 0 ? 0xD9B465u : 0xB8954Au);
      canvas.drawWedgeLine(mx, my, ex, ey, wid, 0.4f, side > 0 ? 0xD9B465u : 0xB8954Au);
    }
  }
}

static void flagBr(int cx, int cy)
{
  canvas.fillSmoothRoundRect(cx - 14, cy - 10, 28, 20, 3, 0x1E9B4Au);
  canvas.fillTriangle(cx, cy - 8, cx + 12, cy, cx, cy + 8, 0xF7D417u);
  canvas.fillTriangle(cx, cy - 8, cx - 12, cy, cx, cy + 8, 0xF7D417u);
  canvas.fillSmoothCircle(cx, cy, 4, 0x1F3F95u);
}

// Bandeira pequena com cantos arredondados e borda clara (qualquer pais). false se nao houver imagem.
static bool smallFlag(int cx, int cy, int w, const char *cc)
{
  const uint16_t *img = (cc && cc[0]) ? flagImage(cc) : nullptr;
  if(!img) return false;
  const int h = w * 3 / 4, x0 = cx - w / 2, y0 = cy - h / 2, r = max(2, w / 10);
  uint16_t *buf = (uint16_t *)canvas.getBuffer();
  gfxBlendRoundRect(x0 - 1, y0 - 1, w + 2, h + 2, r + 1, 0x000000u, 90);
  for(int y = 0; y < h; y++)
    for(int x = 0; x < w; x++)
    {
      const int px = x0 + x, py = y0 + y; if((unsigned)px >= LCD_W || (unsigned)py >= LCD_H) continue;
      // cantos arredondados
      const int ex = x < r ? r - x : x >= w - r ? x - (w - r - 1) : 0, ey = y < r ? r - y : y >= h - r ? y - (h - r - 1) : 0;
      if(ex && ey && ex * ex + ey * ey > r * r) continue;
      // media de 4 amostras: a bandeira reduzida fica sem serrilhado
      int R = 0, G = 0, B = 0;
      for(int k = 0; k < 4; k++)
      {
        const uint32_t c = flagPixel(img, (x + 0.25f + 0.5f * (k & 1)) / w, (y + 0.25f + 0.5f * (k >> 1)) / h);
        R += (c >> 16) & 255; G += (c >> 8) & 255; B += c & 255;
      }
      const uint16_t v = (((R / 4) & 0xF8) << 8) | (((G / 4) & 0xFC) << 3) | ((B / 4) >> 3);
      buf[py * LCD_W + px] = (v >> 8) | (v << 8);
    }
  return true;
}

static bool brazilian(const char *c)
{
  return (c[0] == 'P' && c[1] >= 'P' && c[1] <= 'Y') || (c[0] == 'Z' && c[1] >= 'V' && c[1] <= 'Z');
}

static void drawAurora(const Row *rs, const Who *info, int n, const char *st, uint32_t since)
{
  auroraBg();
  if(!n)
  {
    gfxTextSpaced(T("DMR ID","ID DMR","ID DMR"), CX, 70, FxCinzel14, GOLD, 3);
    gfxWrapRoundCentered(st[0] ? st : "Aguardando…", CY, FxSerif22, st[0] ? 0xFFD6A0u : LAV, 28, 3, 40);
    return;
  }
  char b[64];
  if(!listView)
  {
    const Row &r = rs[0];
    const Who &w = info[0];
    laurel(true); laurel(false);
    if(r.live)
    {
      const float el = (millis() - since) / 1000.0f;
      gfxArc(CX, CY, 158, 161, 0, 359.99f, GOLD, 45);
      gfxArc(CX, CY, 158, 161, 0, min(359.9f, el * 3.0f), GOLD, 255);   // 2 min = volta completa
      gfxTextSpaced("TRANSMITINDO", CX, 80, FxCinzel14, GOLD, 3);
    }
    else
    {
      snprintf(b, sizeof(b), "ÚLTIMO ÀS %s", r.when[0] ? r.when : "--:--");
      gfxTextSpaced(b, CX, 80, FxCinzel14, LAV, 2);
    }
    gfxText(r.call, CX, 128, FxCinzel34, 0xFFFFFFu);
    if(w.name[0]) gfxTextFit(w.name, CX, 170, 250, FxSerif30, GOLD);
    if(w.place[0]) gfxTextFit(w.place, CX, 202, 230, FontSmall, LAV);
    if(!smallFlag(CX, 230, 30, w.cc) && brazilian(r.call)) flagBr(CX, 230);
    char tg[24]; tgText(tg, sizeof(tg), r.tg);
    for(char *p = tg; *p; p++) *p = toupper(*p);
    snprintf(b, sizeof(b), "%s%s%s", tg, r.slot[0] ? " · " : "", r.slot);
    gfxTextSpaced(b, CX, 262, FxCinzel14, LAV, 2);
    if(r.live) { const uint32_t e = (millis() - since) / 1000; snprintf(b, sizeof(b), "%02lu:%02lu", (unsigned long)(e / 60), (unsigned long)(e % 60)); }
    else if(r.dur) snprintf(b, sizeof(b), T("%d s on air","%d s no ar","%d s en aire"), r.dur);
    else b[0] = 0;
    if(b[0]) gfxText(b, CX, 294, FxSerif30, 0xFFFFFFu);
    if(w.id) { snprintf(b, sizeof(b), "ID %lu", (unsigned long)w.id); gfxText(b, CX, 324, FontSmall, 0x8C88B8u); }
    return;
  }
  gfxTextSpaced("ÚLTIMOS OUVIDOS", CX, 64, FxCinzel14, GOLD, 3);
  const int rowsShown = min(n, 6);
  for(int i = 0; i < rowsShown; i++)
  {
    const int y = 104 + i * 38;
    const int half = gfxRoundHalfWidth(y, 22);
    gfxText(rs[i].call, CX - half + 6, y, FxCinzel20, rs[i].live ? 0xFF9AA8u : 0xFFFFFFu, middle_left);
    if(info[i].name[0]) gfxTextFit(info[i].name, CX + 14, y, 84, FxSerif22, LAV);
    snprintf(b, sizeof(b), "%lu", (unsigned long)rs[i].tg);
    gfxText(b, CX + half - 6, y - 8, FontSmall, GOLD, middle_right);
    gfxText(rs[i].when, CX + half - 6, y + 10, FontSmall, 0x8C88B8u, middle_right);
    if(i < rowsShown - 1) gfxBlendLine(CX - half + 20, y + 19, CX + half - 20, y + 19, GOLD, 50);
  }
}

#include "DmrThemes.inc"

void dmrDraw()
{
  lock();
  const int n = rowCount;
  Row rs[MAX_ROWS]; memcpy(rs, rows, sizeof(Row) * n);
  char st[64]; strlcpy(st, status, sizeof(st));
  Who info[MAX_ROWS];
  for(int i = 0; i < n; i++) { Who *w = findWho(rs[i].call); if(w) info[i] = *w; else memset(&info[i], 0, sizeof(Who)); }
  const uint32_t since = liveSince;
  unlock();
  // sem cadastro (consulta desligada ou ainda nao feita): pais pelo prefixo do indicativo
  for(int i = 0; i < n && i < 6; i++)
    if(!info[i].cc[0] && flagCountryByCall(rs[i].call, info[i].cc)) flagCountryByCode(info[i].cc, info[i].country, sizeof(info[i].country));

  switch(feat.dStyle)
  {
    case 0: drawAurora(rs, info, n, st, since); return;
    case 2: drawFlagBg(rs, info, n, st, since); return;
    case 3: drawSash(rs, info, n, st, since); return;
    case 4: drawPixel(rs, info, n, st, since); return;
    case 5: drawRing(rs, info, n, st, since); return;
    default: break;
  }
  gfxBackground();
  gfxText(T("DMR ID","ID DMR","ID DMR"), CX, 44, FontSmall, C_MUTED);
  if(!n) { gfxStatusLine(st[0] ? st : "Aguardando…", st[0] ? C_WARN : C_MUTED); return; }

  if(!listView)
  {
    const Row &r = rs[0];
    const Who &w = info[0];
    char b[64];
    if(r.live)
    {
      canvas.fillSmoothRoundRect(CX - 52, 62, 104, 28, 14, 0x5A1212u);
      canvas.fillSmoothCircle(CX - 32, 76, 5, ((millis() / 500) & 1) ? 0xFF4A4Au : 0x8A2020u);
      gfxText("NO AR", CX + 8, 76, FontSmall, 0xFFB0B0u);
    }
    else { snprintf(b, sizeof(b), T("Last at %s","Último às %s","Último a las %s"), r.when[0] ? r.when : "--:--"); gfxText(b, CX, 76, FontSmall, C_MUTED); }
    gfxText(r.call, CX, 132, FontTitle, C_ACCENT);
    if(w.name[0]) gfxTextFit(w.name, CX, 172, 270, FontTitle, C_TEXT);
    if(w.place[0]) gfxTextFit(w.place, CX, 206, 270, FontBody, C_MUTED);
    else if(w.country[0]) gfxTextFit(w.country, CX, 206, 270, FontBody, C_MUTED);
    smallFlag(CX + 120, 132, 30, w.cc);
    char tg[24]; tgText(tg, sizeof(tg), r.tg);
    snprintf(b, sizeof(b), "%s%s%s", tg, r.slot[0] ? "  ·  " : "", r.slot);
    gfxText(b, CX, 246, FontBody, C_TEXT);
    if(r.live) snprintf(b, sizeof(b), "%lus", (unsigned long)((millis() - since) / 1000));
    else if(r.dur) snprintf(b, sizeof(b), T("duration %ds","duração %ds","duración %ds"), r.dur);
    else b[0] = 0;
    if(b[0]) gfxText(b, CX, 276, FontSmall, C_MUTED);
    if(w.id) { snprintf(b, sizeof(b), "ID %lu", (unsigned long)w.id); gfxText(b, CX, 302, FontSmall, C_MUTED); }
    return;
  }

  // Lista
  gfxText(T("Recently heard","Últimos ouvidos","Escuchados recientemente"), CX, 70, FontBody, C_ACCENT);
  const int rowsShown = min(n, 6);
  for(int i = 0; i < rowsShown; i++)
  {
    const int y = 108 + i * 36;
    const int half = gfxRoundHalfWidth(y, 20);
    gfxText(rs[i].call, CX - half + 4, y, FontBody, rs[i].live ? 0xFF8A8Au : C_TEXT, middle_left);
    char b[40];
    if(info[i].name[0]) gfxTextFit(info[i].name, CX + 8, y, 80, FontSmall, C_MUTED);
    snprintf(b, sizeof(b), "%lu", (unsigned long)rs[i].tg);
    gfxText(b, CX + half - 4, y - 7, FontSmall, C_TEXT, middle_right);
    gfxText(rs[i].when, CX + half - 4, y + 10, FontSmall, C_MUTED, middle_right);
  }
}
