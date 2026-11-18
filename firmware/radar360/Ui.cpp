#include "Ui.h"
#include <math.h>
#include "Gfx.h"
#include "Config.h"
#include "Feat.h"
#include "Lang.h"
#include "Net.h"
#include "Version.h"
#include "Flights.h"
#include "Weather.h"
#include "Photos.h"
#include "News.h"
#include "Dmr.h"
#include "Crash.h"
#include "Http.h"
#include "Clock.h"
#include "Horo.h"
#include "Moon.h"
#include "Pc.h"
#include "Quotes.h"
#include "Wifi.h"
#include "LanScan.h"
#include "Sky.h"
#include <time.h>
#include <esp_heap_caps.h>

static const char *screenName(uint8_t id)
{
  switch(id)
  {
    case SCR_CLOCK:    return T("Clock", "Relógio", "Reloj");
    case SCR_FLIGHTS:  return T("Flights", "Voos", "Vuelos");
    case SCR_WX_RADAR: return T("Rain radar", "Radar de chuva", "Radar de lluvia");
    case SCR_WX_NOW:   return T("Weather now", "Tempo agora", "Tiempo ahora");
    case SCR_WX_HOURS: return T("Next hours", "Próximas horas", "Próximas horas");
    case SCR_WX_DAYS:  return T("Next days", "Próximos dias", "Próximos días");
    case SCR_PHOTOS:   return T("Photo frame", "Porta-retrato", "Marco de fotos");
    case SCR_NEWS:     return T("News", "Notícias", "Noticias");
    case SCR_DMR:      return T("DMR ID", "ID DMR", "ID DMR");
    case SCR_HORO:     return T("Horoscope", "Horóscopo", "Horóscopo");
    case SCR_MOON:     return T("Moon", "Lua", "Luna");
    case SCR_PC:       return T("PC", "PC", "PC");
    case SCR_NETWORK:  return T("Network", "Rede", "Red");
    case SCR_QUOTES:   return T("Quotes", "Cotações", "Cotizaciones");
    case SCR_WIFI:     return T("Wi-Fi", "Wi-Fi", "Wi-Fi");
  }
  return "?";
}

// ---- Estado ----
static volatile uint8_t current = SCR_CLOCK;
static bool qrOpen = false;
static uint8_t netPage = 0;    // tela Rede: 0 = info, 1 = equipamentos
static int netSel = 0;         // rolagem da lista de equipamentos
static uint8_t qrPage = 0;
static volatile bool dirty = true;
static time_t lastDrawnSec = 0;
static uint32_t lastActivity = 0;
static uint32_t lastFrame = 0;
static uint32_t frameCost = 20;   // ms do ultimo quadro (desenho + envio)
static UiPerf perf = {};
static void perfUpdate();
static volatile bool turboTestReq = false;
static uint32_t turboTestUntil = 0;   // != 0: mostrando a tela de teste do modo turbo
static uint8_t brightNow = 0;
static uint32_t lastBrightCheck = 0;

static volatile uint8_t pendingBtn = BTN_NONE;
static volatile int pendingGoto = -1;
static volatile bool screenOn = true;
static volatile bool screenOnChanged = false;
static volatile int otaPct = -1;
static volatile bool cfgChanged = false;
static char msgText[64];
static volatile uint32_t msgUntil = 0;
static volatile bool msgNew = false;

bool uiScreenEnabled(uint8_t id)
{
  switch(id)
  {
    case SCR_CLOCK: return true;
    case SCR_FLIGHTS: return feat.scrFlights;
    case SCR_WX_RADAR: return feat.scrWxRadar;
    case SCR_WX_NOW: return feat.scrWxNow;
    case SCR_WX_HOURS: return feat.scrWxHours;
    case SCR_WX_DAYS: return feat.scrWxDays;
    case SCR_PHOTOS: return feat.scrPhotos;
    case SCR_NEWS: return feat.scrNews;
    case SCR_DMR: return feat.scrDmr;
    case SCR_HORO: return feat.scrHoro;
    case SCR_MOON: return feat.scrMoon;
    case SCR_PC: return feat.scrPc;
    case SCR_NETWORK: return feat.scrNetwork;
    case SCR_QUOTES: return feat.scrQuotes;
    case SCR_WIFI: return feat.scrWifi;
    default: return false;
  }
}

static bool setupScreen()
{
  return !netConnected() && netApActive() && (netSetupMode() || !configHasWifi());
}

static void onLeave(uint8_t id)
{
  if(id == SCR_FLIGHTS) flightsClose();
  if(id == SCR_PC) pcClose();
  if(id == SCR_QUOTES) quotesClose();
  if(id == SCR_WIFI) wifiClose();
}

static void onEnter(uint8_t id)
{
  switch(id)
  {
    case SCR_FLIGHTS: flightsOpen(); break;
    case SCR_PHOTOS: photosOpen(); break;
    case SCR_NEWS: newsOpen(); break;
    case SCR_DMR: dmrOpen(); break;
    case SCR_HORO: horoOpen(); break;
    case SCR_PC: pcOpen(); break;
    case SCR_QUOTES: quotesOpen(); break;
    case SCR_WIFI: wifiOpen(); break;
    case SCR_NETWORK: netPage = 0; netSel = 0; break;
    case SCR_WX_NOW: case SCR_WX_HOURS: case SCR_WX_DAYS: weatherOpen(); break;
    default: break;
  }
}

// ---- Transicao suave entre telas (esmaece a tela anterior em ~0,25 s) ----
static uint16_t *transBuf = nullptr;
static uint32_t transAt = 0;
static const uint32_t TRANS_MS = 260;
static void startTransition()
{
  if(!transBuf) transBuf = (uint16_t *)heap_caps_malloc(LCD_W * LCD_H * 2, MALLOC_CAP_SPIRAM);
  const uint16_t *f = displayFrame();
  if(!transBuf || !f) return;
  memcpy(transBuf, f, LCD_W * LCD_H * 2);
  transAt = millis() | 1;
}
static bool transRunning() { return transAt && millis() - transAt < TRANS_MS; }
static void applyTransition()
{
  if(!transAt) return;
  const uint32_t t = millis() - transAt;
  if(t >= TRANS_MS) { transAt = 0; return; }
  float k = 1.0f - (float)t / TRANS_MS; k = k * k;          // anterior some rapido no fim
  const uint8_t a = (uint8_t)(k * 255);
  uint16_t *dst = (uint16_t *)canvas.getBuffer();
  for(int i = 0; i < LCD_W * LCD_H; i++)
  {
    const uint16_t o = (uint16_t)(transBuf[i] >> 8 | transBuf[i] << 8);
    dst[i] = gfxBlend(dst[i], ((o >> 11) << 19) | (((o >> 5) & 63) << 10) | ((o & 31) << 3), a);
  }
}

static void go(uint8_t id)
{
  if(id >= SCR_COUNT || !uiScreenEnabled(id)) id = SCR_CLOCK;
  if(id != current) { startTransition(); onLeave(current); current = id; onEnter(id); }
  qrOpen = false;
  dirty = true;
}

static_assert(FEAT_SCREENS == SCR_COUNT, "FEAT_SCREENS (Feat.h) precisa ser igual a SCR_COUNT");

static int orderPos(uint8_t id) { for(int i = 0; i < SCR_COUNT; i++) if(feat.order[i] == id) return i; return 0; }

static void next()
{
  int pos = orderPos(current);
  uint8_t id = current;
  for(int k = 0; k < SCR_COUNT; k++)
  {
    pos = (pos + 1) % SCR_COUNT;
    id = feat.order[pos];
    if(uiScreenEnabled(id)) break;
  }
  go(id);
}

static void prev()
{
  int pos = orderPos(current);
  uint8_t id = current;
  for(int k = 0; k < SCR_COUNT; k++)
  {
    pos = (pos + SCR_COUNT - 1) % SCR_COUNT;
    id = feat.order[pos];
    if(uiScreenEnabled(id)) break;
  }
  go(id);
}

static bool cyclePaused = false;   // "pausar troca" do controle remoto
bool uiCyclePaused() { return cyclePaused; }

// ◀ ▶: dentro da tela quando ela tem itens (avioes nos detalhes, manchetes, fotos, signos); senao troca de tela
static void step(int d)
{
  const uint8_t cur = current;
  if(cur == SCR_FLIGHTS && flightsStep(d)) return;
  if(cur == SCR_NEWS && newsStep(d)) return;
  if(cur == SCR_PHOTOS && photosStep(d)) return;
  if(cur == SCR_HORO && feat.hSigns != 0 && __builtin_popcount(feat.hSigns) > 1) { horoStep(d); return; }
  if(cur == SCR_HORO && feat.hSigns == 0) { horoStep(d); return; }
  if(d > 0) next(); else prev();
}

static void pageDots()
{
  int n = 0, pos = 0;
  for(int k = 0; k < SCR_COUNT; k++) { const uint8_t i = feat.order[k]; if(uiScreenEnabled(i)) { if(i == current) pos = n; n++; } }
  if(n < 2) return;
  if(current == SCR_CLOCK && feat.cStyle != CLK_SIMPLE && feat.cStyle != CLK_REAL) return;   // mostradores cheios
  if(current == SCR_HORO) return;
  const bool overImage = (current == SCR_CLOCK && feat.cStyle == CLK_REAL) || (current == SCR_WX_NOW && feat.wNowStyle == 0);
  // Bolinhas ao longo da curva da borda redonda (nao mais numa linha reta): acompanham o circulo da tela,
  // centradas na base. Com muitas paginas, as pontas sobem pela curva em vez de encostar nas laterais.
  const int yBottom = current == SCR_CLOCK ? 304 : 344;
  const float R = (float)(yBottom - CY);          // raio do arco a partir do centro da tela
  float dA = 12.0f / R;                            // passo angular ~ 12 px de arco entre bolinhas
  const float maxSpan = 2.44f;                     // no maximo ~140 graus de abertura
  if((n - 1) * dA > maxSpan) dA = maxSpan / (n - 1);
  const float a0 = 1.5707963f - (n - 1) * dA * 0.5f;   // 90 graus = base da tela  // simetrico em torno da base (90 graus)
  for(int i = 0; i < n; i++)
  {
    const float a = a0 + i * dA;
    const int x = (int)lroundf(CX + R * cosf(a));
    const int y = (int)lroundf(CY + R * sinf(a));
    if(overImage) canvas.fillSmoothCircle(x, y + 1, i == pos ? 4 : 3, 0x0A0F18u);
    canvas.fillSmoothCircle(x, y, i == pos ? 3 : 2, overImage ? (i == pos ? 0xFFFFFFu : 0x9AA8B8u) : (i == pos ? C_ACCENT : C_RING2));
  }
}

// ---- Rede ----
static void row(const char *label, const char *value, int y, uint32_t vc = C_TEXT)
{
  gfxText(label, CX, y, FontSmall, C_MUTED);
  gfxTextFit(value, CX, y + 24, 270, FontBody, vc);
}

static LanDev netRows[48];

// Entra/sai da pagina de equipamentos (OK na tela Rede). Dispara a varredura se estiver velha.
static void netScreenToggle()
{
  netPage ^= 1;
  if(netPage == 1) { netSel = 0; if(!lanScanning() && (lanScanAge() == 0 || lanScanAge() > 30000)) lanScanStart(); }
}

static void drawNetworkDevices()
{
  gfxBackground();
  gfxText(T("Devices","Equipamentos","Dispositivos"), CX, 54, FontTitle, C_ACCENT);
  const int n = lanList(netRows, 48);
  char buf[48];
  if(lanScanning())
  {
    snprintf(buf, sizeof(buf), "Procurando… %d%%", lanProgress());
    gfxText(buf, CX, 92, FontSmall, C_MUTED);
    const int bw = 200, bx = CX - bw / 2, by = 108;        // barra de progresso
    canvas.drawRoundRect(bx, by, bw, 8, 4, C_RING2);
    canvas.fillRoundRect(bx, by, bw * lanProgress() / 100, 8, 4, C_ACCENT);
  }
  else
  {
    snprintf(buf, sizeof(buf), "%d na rede", n);
    gfxText(buf, CX, 96, FontBody, C_TEXT);
    gfxText(T("OK: back  ·  ↑↓: scroll","OK: voltar  ·  ↑↓: rolar","OK: volver  ·  ↑↓: desplazar"), CX, 320, FxInter13, C_MUTED);
  }
  if(n == 0)
  {
    if(!lanScanning()) gfxText(T("None found","Nenhum encontrado","Ninguno"), CX, 180, FontBody, C_MUTED);
    return;
  }
  const int top = lanScanning() ? 132 : 124, rowH = 42, vis = 4;
  if(netSel > n - 1) netSel = n - 1;
  if(netSel < 0) netSel = 0;
  int first = netSel - vis / 2; if(first > n - vis) first = n - vis; if(first < 0) first = 0;
  for(int i = first; i < n && i < first + vis; i++)
  {
    const LanDev &d = netRows[i];
    const int y = top + (i - first) * rowH;
    const bool sel = (i == netSel && !lanScanning());
    if(sel) canvas.fillSmoothRoundRect(CX - 150, y - 4, 300, rowH - 6, 8, 0x16202Eu);
    const char *prim = d.name[0] ? d.name : (d.vendor ? d.vendor : "—");
    gfxTextFit(prim, CX, y + 2, 280, FontBody, sel ? C_ACCENT : C_TEXT);
    if(d.name[0] && d.vendor) snprintf(buf, sizeof(buf), "192.168.%u.%u · %s", d.ip[2], d.ip[3], d.vendor);
    else snprintf(buf, sizeof(buf), "192.168.%u.%u", d.ip[2], d.ip[3]);
    gfxText(buf, CX, y + 22, FxInter13, C_MUTED);
  }
}

static void drawNetwork()
{
  gfxBackground();
  gfxText(T("Network","Rede","Red"), CX, 62, FontTitle, C_ACCENT);
  char buf[64];
  if(netConnected())
  {
    row("Wi-Fi", netSsid().c_str(), 100);
    row(T("Address","Endereço","Dirección"), netIp().c_str(), 152);
    snprintf(buf, sizeof(buf), "%s.local", cfg.host);
    row(T("Name","Nome","Nombre"), buf, 204);
    snprintf(buf, sizeof(buf), "%d dBm", netRssi());
    row(T("Signal","Sinal","Señal"), buf, 256, netRssi() > -70 ? C_ACCENT : C_WARN);
  }
  else if(netApActive())
  {
    row(T("Access point","Ponto de acesso","Punto de acceso"), netApSsid().c_str(), 110, C_WARN);
    row(T("Open in browser","Abra no navegador","Abre en el navegador"), "192.168.4.1", 170);
    if(configDefaultPassword()) gfxText(T("Login: admin  ·  pass: radar360","Login: admin  ·  senha: radar360","Acceso: admin  ·  clave: radar360"), CX, 232, FontSmall, C_MUTED);
    gfxText(T("Long press: QR code","Toque longo: QR code","Pulsación larga: QR"), CX, 262, FontSmall, C_MUTED);
  }
  else row("Wi-Fi", T("Searching network…","Procurando rede…","Buscando red…"), 140, C_WARN);

  if(!netConnected() && netLastError()[0])
  {
    snprintf(buf, sizeof(buf), "Falha: %s", netLastError());
    gfxTextFit(buf, CX, 298, 230, FontSmall, C_BAD);
  }
  else
  {
    snprintf(buf, sizeof(buf), "v%s", FW_VERSION);
    gfxText(buf, CX, 298, FontSmall, C_MUTED);
  }
}

static void drawQr()
{
  canvas.fillScreen(C_BG);
  char data[96], title[48], line[64];
  if(!netConnected() && netApActive() && qrPage == 0)
  {
    snprintf(data, sizeof(data), "WIFI:T:nopass;S:%s;;", netApSsid().c_str());
    strlcpy(title, T("1. Connect to Wi-Fi","1. Conecte no Wi-Fi","1. Conéctate al Wi-Fi"), sizeof(title));
    strlcpy(line, netApSsid().c_str(), sizeof(line));
  }
  else
  {
    // conectado: o toque curto alterna entre o painel e o controle remoto (/control)
    const bool ctl = netConnected() && qrPage == 0;
    snprintf(data, sizeof(data), "http://%s/%s", netIp().c_str(), ctl ? "control" : "");
    if(netApActive() && !netConnected()) snprintf(title, sizeof(title), T("2. Open %s","2. Abra %s","2. Abre %s"), netIp().c_str());
    else strlcpy(title, ctl ? T("Remote control","Controle remoto","Control remoto") : T("Web panel","Painel web","Panel web"), sizeof(title));
    if(configDefaultPassword()) strlcpy(line, T("login admin · radar360","login admin · radar360","acceso admin · radar360"), sizeof(line));
    else snprintf(line, sizeof(line), "%s", netIp().c_str());
  }
  const int size = 196, x = CX - size / 2, y = 92;
  canvas.fillSmoothRoundRect(x - 12, y - 12, size + 24, size + 24, 14, TFT_WHITE);
  canvas.qrcode(data, x, y, size, 4);
  gfxText(title, CX, 52, FontBody, C_ACCENT);
  gfxText(line, CX, 318, FontSmall, C_TEXT);
}

static volatile uint32_t otaFrames = 0;
uint32_t uiOtaFrames() { return otaFrames; }

static void drawOta(int pct)
{
  otaFrames++;
  canvas.fillScreen(C_BG);
  canvas.fillArc(CX, CY, 150, 140, 0, 360, C_RING);
  if(pct > 0) canvas.fillArc(CX, CY, 150, 140, -90, -90 + pct * 360 / 100, C_ACCENT);
  char buf[16]; snprintf(buf, sizeof(buf), "%d%%", pct);
  gfxText(T("Updating","Atualizando","Actualizando"), CX, 130, FontBody, C_MUTED);
  gfxText(buf, CX, 180, FontTitle, C_TEXT);
  gfxText(T("Do not power off","Não desligue","No apague"), CX, 235, FontSmall, C_WARN);
}

static void drawMessage()
{
  const int w = min(300, gfxTextWidth(msgText, FontBody) + 40), h = 46;
  canvas.fillSmoothRoundRect(CX - w / 2, 270 - h / 2, w, h, 23, 0x18332Eu);
  canvas.drawRoundRect(CX - w / 2, 270 - h / 2, w, h, 23, C_ACCENT);
  gfxTextFit(msgText, CX, 270, w - 24, FontBody, C_TEXT);
}

// ---- Brilho ----
static uint8_t wantedBrightness()
{
  if(!screenOn) return 0;
  if(cfg.nightOn && netTimeValid())
  {
    time_t now = time(nullptr); struct tm t; localtime_r(&now, &t);
    const int m = t.tm_hour * 60 + t.tm_min;
    const bool night = cfg.nightStart <= cfg.nightEnd ? (m >= cfg.nightStart && m < cfg.nightEnd)
                                                      : (m >= cfg.nightStart || m < cfg.nightEnd);
    if(night) return cfg.nightBright;
  }
  return cfg.bright;
}

static void applyBrightness(bool force)
{
  const uint8_t b = wantedBrightness();
  if(force || b != brightNow) { brightNow = b; displaySetBrightness(b); }
}

// ---- Teste do fio da tela a 80 MHz: padrao que denuncia erro de transmissao ----
static void drawTurboTest()
{
  const uint32_t ms = millis();
  canvas.fillScreen(0x000000u);
  // faixas de cores puras e degrades (erro de bit vira cor errada ou riscos)
  static const uint32_t C[] = {0xFFFFFFu, 0xFFFF00u, 0x00FFFFu, 0x00FF00u, 0xFF00FFu, 0xFF0000u, 0x0000FFu};
  for(int i = 0; i < 7; i++) canvas.fillRect(30 + i * 43, 70, 43, 60, C[i]);
  for(int x = 30; x < 330; x++)
  {
    const int v = (x - 30) * 255 / 299;
    canvas.drawFastVLine(x, 136, 18, ((uint32_t)v << 16) | (v << 8) | v);
    canvas.drawFastVLine(x, 156, 10, (uint32_t)v << 16);
    canvas.drawFastVLine(x, 168, 10, (uint32_t)v << 8);
    canvas.drawFastVLine(x, 180, 10, (uint32_t)v);
  }
  // linhas finas alternadas e um quadrado que anda
  for(int y = 196; y < 216; y += 2) canvas.drawFastHLine(40, y, 280, 0xFFFFFFu);
  for(int x = 40; x < 320; x += 2) canvas.drawFastVLine(x, 218, 16, 0xFFFFFFu);
  const int bx = 40 + (int)((ms / 8) % 260);
  canvas.fillRect(bx, 196, 20, 38, 0xFF3B30u);
  gfxText(T("TURBO SCREEN · 80 MHz","TELA TURBO · 80 MHz","PANTALLA TURBO · 80 MHz"), CX, 46, FontSmall, C_ACCENT);
  gfxText(T("Is the image perfect?","A imagem está perfeita?","¿La imagen está perfecta?"), CX, 262, FontBody, C_TEXT);
  gfxText(T("Press the button to keep","Aperte o botão para manter","Pulsa el botón para mantener"), CX, 290, FontSmall, C_MUTED);
  char b[40]; snprintf(b, sizeof(b), "volta para 40 MHz em %lu s", (unsigned long)((turboTestUntil - ms + 999) / 1000));
  gfxText(b, CX, 316, FontSmall, C_WARN);
}

// ---- Botao ----
static void handleButton(ButtonEvent e)
{
  lastActivity = millis();
  if(e == BTN_POWER) { screenOn = !screenOn; applyBrightness(true); dirty = true; return; }
  if(!screenOn) { screenOn = true; applyBrightness(true); dirty = true; return; }   // qualquer toque acende
  const uint8_t cur = current;
  switch(e)
  {
    case BTN_SHORT:
      if(qrOpen || setupScreen()) qrPage ^= 1;
      else if(cur == SCR_FLIGHTS && flightsShortPress()) {}
      else if(cur == SCR_NEWS && newsShortPress()) {}
      else if(cur == SCR_WIFI && wifiShortPress()) {}
      else next();
      break;
    case BTN_LONG:
      if(qrOpen) qrOpen = false;
      else if(cur == SCR_FLIGHTS) flightsLongPress();
      else if(cur == SCR_WX_RADAR) weatherRadarLongPress();
      else if(cur == SCR_PHOTOS) photosLongPress();
      else if(cur == SCR_NEWS) newsLongPress();
      else if(cur == SCR_DMR) dmrLongPress();
      else if(cur == SCR_HORO) horoLongPress();
      else if(cur == SCR_WIFI) wifiShortPress();      // OK: alterna lista/canais (nao abre o QR)
      else if(cur == SCR_NETWORK) netScreenToggle();  // OK: alterna info/equipamentos
      else { qrOpen = true; qrPage = netConnected() ? 1 : 0; }
      break;
    case BTN_DOUBLE:
    case BTN_HOME:
      flightsClose();
      go(SCR_CLOCK);
      break;
    case BTN_NEXT: if(qrOpen) qrPage ^= 1; else step(+1); break;
    case BTN_PREV: if(qrOpen) qrPage ^= 1; else step(-1); break;
    case BTN_OK: handleButton(BTN_LONG); return;
    case BTN_UP: case BTN_DOWN:
    {
      if(cur == SCR_WIFI) { wifiScroll(e == BTN_UP ? -1 : +1); dirty = true; break; }
      if(cur == SCR_NETWORK && netPage == 1) { netSel += (e == BTN_UP ? -1 : +1); dirty = true; break; }
      cfg.bright = constrain((int)cfg.bright + (e == BTN_UP ? 10 : -10), 5, 100);
      configSave();
      applyBrightness(true);
      char b[24]; snprintf(b, sizeof(b), "Brilho %d%%", cfg.bright); uiPostMessage(b, 1200);
      break;
    }
    case BTN_PAUSE:
      cyclePaused = !cyclePaused;
      uiPostMessage(cyclePaused ? "Troca automática pausada" : "Troca automática ligada", 1800);
      break;
    default: return;
  }
  dirty = true;
}

static uint32_t frameInterval()
{
  if(qrOpen || otaPct >= 0) return 0;
  if(turboTestUntil) return 40;
  if(transRunning()) return 20;
  switch(current)
  {
    case SCR_FLIGHTS: return flightsAnimated() ? 40 : 0;
    case SCR_WX_RADAR: return weatherRadarCardAnimated() ? 50 : weatherRadarAnimated() ? 100 : 0;
    case SCR_PHOTOS: return photosAnimated() ? 40 : 0;
    case SCR_DMR: return dmrAnimated() ? 500 : 0;
    case SCR_NEWS: return newsAnimated() ? 40 : 0;
    case SCR_CLOCK: return clockFrameMs();
    case SCR_HORO: return feat.hTheme == 2 ? 150 : 0;
    case SCR_MOON: return moonFrameMs();
    case SCR_PC: return 40;
    case SCR_WIFI: return wifiAnimated() ? 250 : 0;
    case SCR_NETWORK: return (netPage == 1 && lanScanning()) ? 400 : 0;
    case SCR_WX_NOW: return weatherIntroRunning() ? 30 : weatherNowAnimated() ? skyFrameMs() : 0;
    case SCR_WX_HOURS: case SCR_WX_DAYS: return weatherIntroRunning() ? 30 : 0;
    default: return 0;
  }
}

void uiBegin()
{
  displaySetRotation(cfg.rotation);
  lastActivity = millis();
}

void uiSplash()
{
  canvas.fillScreen(C_BG);
  for(int r = 60; r <= 150; r += 30) canvas.drawCircle(CX, CY, r, C_RING);
  canvas.drawWideLine(CX, CY, CX + 106, CY - 106, 2.0f, C_ACCENT);
  canvas.fillSmoothCircle(CX, CY, 5, C_ACCENT);
  gfxText("RADAR 360", CX, 248, FontTitle, C_TEXT);
  char buf[32]; snprintf(buf, sizeof(buf), "PV2KSM · v%s", FW_VERSION);
  gfxText(buf, CX, 282, FontSmall, C_MUTED);
  displayPush();
  for(int b = 0; b <= cfg.bright; b += 4) { displaySetBrightness(b); delay(12); }
  brightNow = cfg.bright;
}

void uiTick()
{
  const uint32_t now = millis();
  perfUpdate();

  if(turboTestReq)
  {
    turboTestReq = false;
    cfg.lcdTurbo = 2;
    displaySetTurbo(true);
    turboTestUntil = now + 20000;
    screenOn = true; lastActivity = now; applyBrightness(true); dirty = true;
  }
  if(turboTestUntil)
  {
    if(pendingBtn != BTN_NONE)
    {
      pendingBtn = BTN_NONE;
      cfg.lcdTurbo = 1; configSave();
      turboTestUntil = 0; uiPostMessage("Tela turbo ligada", 2500); dirty = true;
    }
    else if((int32_t)(now - turboTestUntil) >= 0)
    {
      cfg.lcdTurbo = 0; displaySetTurbo(false);
      turboTestUntil = 0; uiPostMessage("Turbo desligado", 2500); dirty = true;
    }
  }
  if(pendingBtn != BTN_NONE) { ButtonEvent e = (ButtonEvent)pendingBtn; pendingBtn = BTN_NONE; handleButton(e); }
  if(pendingGoto >= 0) { const int g = pendingGoto; pendingGoto = -1; go(g); screenOn = true; lastActivity = now; applyBrightness(true); }
  if(screenOnChanged) { screenOnChanged = false; applyBrightness(true); dirty = true; }
  if(cfgChanged)
  {
    cfgChanged = false;
    if(!turboTestUntil) displaySetTurbo(cfg.lcdTurbo == 1);
    displaySetRotation(cfg.rotation);
    applyBrightness(true);
    if(!uiScreenEnabled(current)) go(SCR_CLOCK);
    photosRescan();
    dirty = true;
  }
  if(msgNew) { msgNew = false; dirty = true; }
  if(msgUntil && (int32_t)(now - msgUntil) >= 0) { msgUntil = 0; dirty = true; }
  if(now - lastBrightCheck > 5000) { lastBrightCheck = now; applyBrightness(false); }

  // Troca automatica de tela (pausada nos detalhes de voo e QR)
  if(cfg.cycleSecs && !cyclePaused && !qrOpen && otaPct < 0 && !flightsInDetail() && now - lastActivity >= cfg.cycleSecs * 1000UL)
  {
    lastActivity = now;
    next();
  }

  if(current == SCR_NEWS && newsTick()) dirty = true;
  if(current == SCR_HORO && horoTick()) dirty = true;
  if(current == SCR_QUOTES && quotesTick()) dirty = true;

  const time_t sec = time(nullptr);
  if(sec != lastDrawnSec) { lastDrawnSec = sec; dirty = true; }
  // Ritmo das animacoes: o envio a tela agora corre em paralelo (DMA), entao da para manter ~25
  // quadros por segundo; so evita passar do tempo que o quadro leva e alivia um pouco nos downloads.
  uint32_t fi = frameInterval();
  if(fi)
  {
    fi = max(fi, frameCost + 4);
    if(httpBusy()) fi = max(fi, (uint32_t)60);
  }
  if(fi && brightNow && now - lastFrame >= fi) dirty = true;
  if(!dirty) return;
  dirty = false;
  lastFrame = now;

  const int pct = otaPct;
  crumbUi(qrOpen ? "QR" : screenName(current));
  const uint32_t drawT0 = micros();
  if(pct >= 0) drawOta(pct);
  else if(turboTestUntil) drawTurboTest();
  else if(setupScreen() && !qrOpen) drawQr();
  else if(qrOpen) drawQr();
  else
  {
    switch(current)
    {
      case SCR_FLIGHTS: flightsDraw(); break;
      case SCR_WX_RADAR: weatherDrawRadar(); break;
      case SCR_WX_NOW: weatherDrawNow(); break;
      case SCR_WX_HOURS: weatherDrawHours(); break;
      case SCR_WX_DAYS: weatherDrawDays(); break;
      case SCR_PHOTOS: photosDraw(); break;
      case SCR_NEWS: newsDraw(); break;
      case SCR_DMR: dmrDraw(); break;
      case SCR_HORO: horoDraw(); break;
      case SCR_MOON: moonDraw(); break;
      case SCR_PC: pcDraw(); break;
      case SCR_NETWORK: if(netPage == 0) drawNetwork(); else drawNetworkDevices(); break;
      case SCR_QUOTES: quotesDraw(); break;
      case SCR_WIFI: wifiDraw(); break;
      default: clockDraw(); break;
    }
    if(current != SCR_PHOTOS && !(current == SCR_FLIGHTS && !flightsInDetail()) && current != SCR_WX_RADAR) pageDots();
  }
  if(msgUntil && pct < 0) drawMessage();
  applyTransition();
  const float drawMs = (micros() - drawT0) / 1000.0f;
  // a media por tela ignora a transicao (mistura de dois quadros) e o 1o quadro (pode decodificar o fundo)
  static uint8_t lastScr = 255;
  const bool settle = transRunning() || lastScr != current;
  lastScr = current;
  if(pct < 0 && !turboTestUntil && !qrOpen && current < 16 && !settle)
  {
    float &m = perf.screenMs[current];
    m = m == 0 ? drawMs : m * 0.85f + drawMs * 0.15f;
    perf.drawMs = perf.drawMs * 0.8f + drawMs * 0.2f;
  }
  displayPush();
  frameCost = (frameCost * 3 + (millis() - now)) / 4;
}

// medias de desempenho, atualizadas a cada ~2 s (chamado pelo uiTick)
static void perfUpdate()
{
  static uint32_t lastAt = 0;
  static DisplayStats last = {};
  const uint32_t now = millis();
  if(now - lastAt < 2000) return;
  const DisplayStats &s = displayStats();
  const float dt = (now - lastAt) / 1000.0f;
  const uint32_t p = s.pushes - last.pushes, k = s.skipped - last.skipped;
  if(lastAt)
  {
    perf.fps = p / dt;
    perf.pushMs = (p + k) ? (s.pushUs - last.pushUs) / 1000.0f / (p + k) : 0;
    perf.sentPct = p ? 100.0f * (s.px - last.px) / ((float)p * LCD_W * LCD_H) : 0;
    perf.teHz = s.teHz;
    perf.skipPct = (p + k) ? 100.0f * k / (p + k) : 0;
  }
  last = s; lastAt = now;
}

const UiPerf &uiPerf() { return perf; }
void uiStartTurboTest() { turboTestReq = true; }

uint16_t uiWantedCpuMhz()
{
  if(!cfg.cpuAuto) return cfg.cpuMhz;
  if(otaPct >= 0) return cfg.cpuMhz;
  if(brightNow == 0) return 80;                                         // tela apagada (noite)
  if(millis() - lastActivity > 30000 && frameInterval() == 0) return cfg.cpuMhz > 160 ? 160 : cfg.cpuMhz;   // tela parada
  return cfg.cpuMhz;
}

void uiPostButton(ButtonEvent e) { pendingBtn = e; }
void uiPostGoto(uint8_t screen) { pendingGoto = screen; }
void uiPostMessage(const char *t, uint32_t ms)
{
  strlcpy(msgText, t, sizeof(msgText));
  msgUntil = millis() + ms;
  if(!msgUntil) msgUntil = 1;
  msgNew = true;
}
void uiSetScreenOn(bool on) { screenOn = on; screenOnChanged = true; }
void uiSetOtaProgress(int percent) { otaPct = percent; dirty = true; }
void uiConfigChanged() { cfgChanged = true; }
bool uiScreenOn() { return screenOn; }
uint8_t uiCurrent() { return current; }
const char *uiScreenName(uint8_t id) { return id < SCR_COUNT ? screenName(id) : "?"; }
uint8_t uiBrightnessNow() { return brightNow; }
