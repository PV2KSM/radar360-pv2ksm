#include <chrono>
#include <functional>
// Simulador das telas do Radar 360 no PC.
// Compila os mesmos arquivos de desenho do firmware, com dados de exemplo (pasta data/),
// e grava cada tela como imagem em out/*.rgb (convertidas para PNG por render.py).
#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include "Display.h"
#include "Gfx.h"
#include "Pack.h"
#include "Feat.h"
#include "Config.h"
#include "Net.h"
#include "Crash.h"
#include "Http.h"
#include "MapImg.h"
#include "Ui.h"
#include "Clock.h"
#include "Weather.h"
#include "Flights.h"
#include "Photos.h"
#include "News.h"
#include "Dmr.h"
#include "Sky.h"
#include "Horo.h"
#include "Moon.h"
#include "Pc.h"
#include "Quotes.h"
#include "Wifi.h"
void weatherSimRadar(const uint16_t *img, uint8_t view);
#include <sys/stat.h>
#include <unistd.h>
#include <fstream>
#include <sstream>

uint32_t simMillis = 100000;
SerialStub Serial;
WiFiStub WiFi;
MDNSStub MDNS;
FSStub LittleFS;
static std::string simDir;
std::string FSStub::root() { return simDir + "/fs"; }

static time_t simNow = 0;
extern "C" time_t time(time_t *t) { if(t) *t = simNow; return simNow; }

// ---- Tela ----
LGFX_Sprite canvas;
bool displayBegin() { return true; }
void displayPush() {}
void displayWait() {}
void displaySetBrightness(uint8_t) {}
void displaySetRotation(uint8_t) {}
const uint16_t *displayFrame() { return (const uint16_t *)canvas.getBuffer(); }

// ---- Config / rede / UI ----
Config cfg;
void configLoad() {}
void configSave() {}
bool configHasWifi() { return true; }
bool configDefaultPassword() { return false; }
NetState netState() { return NET_CONNECTED; }
bool netConnected() { return true; }
bool netApActive() { return false; }
bool netSetupMode() { return false; }
String netSsid() { return "TERNET_EXT"; }
String netIp() { return "192.168.0.42"; }
String netApSsid() { return "Radar360-1A2B"; }
int netRssi() { return -58; }
bool netTimeValid() { return true; }
const char *netLastError() { return ""; }
int netScanHidden() { return 3; }
int netMyChannel() { return 6; }
void netRequestScan() {}
bool netScanRunning() { return false; }
int netScanResults(const ScanEntry **out) {
  static ScanEntry e[9] = {
    {"TERNET_EXT", -52, 6, true}, {"NET_VIZINHO_2G", -61, 11, true}, {"CLARO_WIFI_A3F1", -68, 1, true},
    {"VIVO-6620", -70, 6, true}, {"Claro_5G_guest", -74, 11, false}, {"AP-Escritorio", -78, 3, true},
    {"OI FIBRA 2.4", -80, 9, true}, {"Repetidor_Sala", -83, 6, true}, {"TP-LINK_7788", -88, 1, true}};
  *out = e; return 9;
}
void netChannelLoad(uint8_t nAp[14], int8_t rssiMax[14]) {
  static const uint8_t ap[14] = {0, 3,0,1,0,0,4,0,0,1,0,3,0,0};
  static const int8_t rs[14] = {-127, -52,-127,-78,-127,-127,-52,-127,-127,-80,-127,-61,-127,-127};
  memcpy(nAp, ap, 14); memcpy(rssiMax, rs, 14);
}
void crumbWorker(const char *) {}
void crumbUi(const char *) {}
static uint8_t simScreen = SCR_CLOCK;
uint8_t uiCurrent() { return simScreen; }
bool uiScreenEnabled(uint8_t) { return true; }

// ---- HTTP com respostas gravadas ----
bool HttpBuf::reserve(size_t n) { data = (uint8_t *)realloc(data, n + 1); cap = n; return data; }
void HttpBuf::release() { free(data); data = nullptr; len = cap = 0; }
bool HttpBuf::grow(size_t need, size_t limit) { if(need + 1 <= cap) return true; if(need > limit) return false; data = (uint8_t *)realloc(data, need + 1); cap = need + 1; return data; }
const char *httpUserAgent() { return "sim"; }
bool httpBusy() { return false; }
// Teste de estresse: respostas trocadas em memoria (chave = trecho da URL)
#include <map>
static std::map<std::string, std::string> overrideData;
static std::map<std::string, int> overrideFail;
int httpGet(const char *url, HttpBuf &out, size_t maxLen, char *err, size_t errLen, const char *, bool, uint32_t)
{
  for(auto &f : overrideFail) if(strstr(url, f.first.c_str())) { if(err) strlcpy(err, "falha simulada", errLen); return f.second; }
  for(auto &o : overrideData)
    if(strstr(url, o.first.c_str()))
    {
      std::string s = o.second;
      if(s.size() > maxLen) s.resize(maxLen);
      out.reserve(s.size()); memcpy(out.data, s.data(), s.size()); out.data[s.size()] = 0; out.len = s.size();
      return 200;
    }
  const char *file = strstr(url, "open-meteo") ? "weather.json" : strstr(url, "adsbdb") ? "route.json" :
                     (strstr(url, "adsb.fi") || strstr(url, "adsb.lol")) ? "flights.json" : strstr(url, "news.google") ? "news.xml" :
                     strstr(url, "lh.php") ? "lh.html" : strstr(url, "radioid") ? "radioid.json" : nullptr;
  if(!file) { if(err) strlcpy(err, "sem dados no simulador", errLen); return -1; }
  std::ifstream f(simDir + "/data/" + file, std::ios::binary);
  if(!f) { if(err) strlcpy(err, "arquivo faltando", errLen); return 404; }
  std::stringstream ss; ss << f.rdbuf(); std::string s = ss.str();
  // marca de tempo: {{NOW}} vira o instante simulado
  for(size_t p; (p = s.find("{{NOW}}")) != std::string::npos;) s.replace(p, 7, std::to_string((long)simNow));
  out.reserve(s.size()); memcpy(out.data, s.data(), s.size()); out.data[s.size()] = 0; out.len = s.size();
  return 200;
}

// ---- Mapa (sem mapa no simulador) ----
MapCanvas::~MapCanvas() {}
void MapCanvas::free() {}
LGFX_Sprite *mapBigSprite(int n) { auto *s = new LGFX_Sprite(); s->setColorDepth(16); if(!s->createSprite(n, n)) { delete s; return nullptr; } return s; }
void mapBigRelease(LGFX_Sprite *s) { if(s) { s->deleteSprite(); delete s; } }
bool mapPrepare(MapCanvas &, double, double, double, uint8_t, char *err, size_t n, int) { if(err) strlcpy(err, "", n); return false; }
void mapDownsample(const MapCanvas &, const uint16_t *, uint16_t *) {}
uint32_t mapBgColor(uint8_t) { return 0; }
double mapMetersPerPixel(double, int) { return 1; }
bool mapBaseLoad(double, double, double, uint8_t, uint8_t, uint16_t *) { return false; }
void mapBaseSave(double, double, double, uint8_t, uint8_t, const uint16_t *) {}
const char *mapAttribution(uint8_t) { return ""; }
void mapCacheClear() {}
void mapTint(uint16_t *, uint8_t, uint32_t, size_t) {}

// ---- Gravacao ----
static void save(const char *name)
{
  std::string p = simDir + "/out/" + name + ".rgb";
  FILE *f = fopen(p.c_str(), "wb");
  const uint16_t *b = (const uint16_t *)canvas.getBuffer();
  for(int i = 0; i < LCD_W * LCD_H; i++)
  {
    const uint16_t v = (uint16_t)(b[i] >> 8 | b[i] << 8);
    const uint8_t rgb[3] = {(uint8_t)(((v >> 11) & 31) * 255 / 31), (uint8_t)(((v >> 5) & 63) * 255 / 63), (uint8_t)((v & 31) * 255 / 31)};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
  printf("  %s\n", name);
}

static time_t local(int y, int mo, int d, int h, int mi, int s)
{
  struct tm t = {}; t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d; t.tm_hour = h; t.tm_min = mi; t.tm_sec = s; t.tm_isdst = -1;
  return mktime(&t);
}


// Mapa de mentira com manchas de chuva (o simulador nao baixa o radar)
void flightsSimMap(const uint16_t *img, uint8_t view);
static uint16_t *fakeMap(bool light, bool rain, bool plain);
static void fakeRadar(bool light) { weatherSimRadar(fakeMap(light, true, false), light ? VIEW_BRIGHT : VIEW_DARK); }
static uint16_t *fakeMap(bool light, bool rain, bool plain)
{
  static uint16_t img[LCD_W * LCD_H];
  LGFX_Sprite sp; sp.setColorDepth(16); sp.setBuffer(img, LCD_W, LCD_H, 16);
  const uint32_t land = light ? 0xE6E9E1u : 0x2C4339u, sea = light ? 0xB8D2E2u : 0x22364Au, road = light ? 0xC9CEC2u : 0x3E5A4Cu, txt = light ? 0x7D857Au : 0x84A092u;
  sp.fillScreen(land);
  for(int x = 0; x < LCD_W; x++) { const int y = 250 + (int)(18 * sinf(x / 40.0f)) + x / 6; sp.drawFastVLine(x, y, LCD_H - y, sea); }
  for(int k = 0; k < 9; k++) sp.drawLine((k * 97) % 360, 0, (k * 151 + 60) % 360, 360, road);
  for(int k = 0; k < 6; k++) sp.drawLine(0, k * 70 + 10, 360, k * 50 + 40, road);
  sp.setTextColor(txt); sp.setFont(&fonts::Font2);
  const char *N[] = {"Campinas", "Jundiai", "Sao Paulo", "Taubate", "Santos", "Pouso Alegre", "Sorocaba"};
  for(int k = 0; k < 7; k++) sp.drawString(N[k], 30 + (k * 83) % 270, 40 + (k * 61) % 260);
  if(plain) sp.fillScreen(0x05100Cu);
  struct B { int x, y, r; } bl[] = {{250, 60, 34}, {70, 160, 22}, {290, 200, 26}, {120, 300, 14}};
  if(rain) for(auto &b : bl)
    for(int j = -b.r; j <= b.r; j++) for(int i = -b.r * 2; i <= b.r * 2; i++)
    {
      const float d = sqrtf(i * i / 4.0f + j * j) / b.r; if(d > 1) continue;
      const int x = b.x + i, y = b.y + j; if((unsigned)x >= LCD_W || (unsigned)y >= LCD_H) continue;
      const float n = d + 0.15f * sinf(i * 0.5f) * cosf(j * 0.6f);
      const uint32_t c = n < 0.25f ? 0xE2412Fu : n < 0.5f ? 0xF2B630u : n < 0.8f ? 0x3B82D6u : 0x7FB4F0u;
      sp.drawPixel(x, y, c);
    }
  return img;
}

// LibreHardwareMonitor de mentira: valores que variam com o tempo
static std::string lhmJson(int k, bool gpu = true)
{
  std::ifstream f(simDir + "/data/lhm_template.json"); std::stringstream ss; ss << f.rdbuf(); std::string s = ss.str();
  auto rep = [&](const char *key, const std::string &v) { for(size_t p; (p = s.find(key)) != std::string::npos;) s.replace(p, strlen(key), v); };
  char b[16];
  snprintf(b, sizeof(b), "%.1f", 30 + 25 * sinf(k / 4.0f) + 10 * sinf(k * 1.7f)); for(char *c = b; *c; c++) if(*c == '.') *c = ','; rep("{{CPUL}}", b);
  snprintf(b, sizeof(b), "%.1f", 52 + 8 * sinf(k / 6.0f)); for(char *c = b; *c; c++) if(*c == '.') *c = ','; rep("{{CPUT}}", b);
  snprintf(b, sizeof(b), "%.1f", 70 + 25 * sinf(k / 5.0f + 1)); for(char *c = b; *c; c++) if(*c == '.') *c = ','; rep("{{GPUL}}", b);
  snprintf(b, sizeof(b), "%.1f", 66 + 6 * sinf(k / 7.0f)); for(char *c = b; *c; c++) if(*c == '.') *c = ','; rep("{{GPUT}}", b);
  snprintf(b, sizeof(b), "%.1f", 57.6f + 2 * sinf(k / 9.0f)); for(char *c = b; *c; c++) if(*c == '.') *c = ','; rep("{{MEML}}", b);
  snprintf(b, sizeof(b), "%.1f", 3 + 2.5f * sinf(k / 3.0f)); for(char *c = b; *c; c++) if(*c == '.') *c = ','; rep("{{NET}}", b);
  if(!gpu) { const size_t a = s.find("{\"id\"", s.find("NVIDIA") - 40); (void)a; rep("gpu-nvidia", "xx-nv"); }
  return s;
}
#include "stress.inc"

int main(int argc, char **argv)
{
  simDir = argc > 1 ? argv[1] : ".";
  const std::string only = argc > 2 ? argv[2] : "";
  mkdir((simDir + "/out").c_str(), 0755);
  setenv("TZ", "<-03>3", 1); tzset();
  simNow = local(2026, 11, 13, 10, 32, 15);

  memset(&cfg, 0, sizeof(cfg)); cfg.showSeconds = true; cfg.bright = 80; strlcpy(cfg.host, "radar", sizeof(cfg.host));
  canvas.setColorDepth(16);
  canvas.createSprite(LCD_W, LCD_H);
  printf("pacote: %s\n", getenv("R360_NOPACK") ? "desligado (teste)" : packBegin() ? "ok" : "FALTANDO");
  featLoad();
  feat.lat = 51.5074; feat.lon = -0.1278; strlcpy(feat.place, "London", sizeof(feat.place));
  feat.fCenter = 1; strlcpy(feat.fIcao, "EGLL", sizeof(feat.fIcao)); feat.fStyle = VIEW_PLAIN;
  feat.scrDmr = true; feat.scrNews = true; feat.scrPhotos = true;
  weatherBegin(); flightsBegin(); newsBegin(); dmrBegin(); photosBegin();

  if(only == "stress") { stress(); fflush(stdout); _exit(0); }
  if(only == "prof")
  {
    // tempo de desenho por tela/tema (no PC: so serve para comparar entre si)
    weatherWork();
    auto t = [&](const char *nm, std::function<void()> f) {
      f(); const auto a = std::chrono::steady_clock::now();
      for(int i = 0; i < 40; i++) { simMillis += 40; f(); }
      const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count() / 40;
      printf("%-28s %7.2f ms\n", nm, ms);
    };
    for(int st = 0; st < CLK_COUNT; st++) { feat.cStyle = st; simScreen = SCR_CLOCK; char nm[40]; snprintf(nm, sizeof(nm), "relogio %s", clockStyleName(st)); t(nm, [] { clockDraw(); }); }
    simScreen = SCR_MOON; t("lua", [] { moonDraw(); });
    simScreen = SCR_HORO; t("horoscopo", [] { horoDraw(); });
    simScreen = SCR_WX_HOURS; t("proximas horas", [] { weatherDrawHours(); });
    simScreen = SCR_WX_DAYS; t("proximos dias", [] { weatherDrawDays(); });
    simScreen = SCR_WX_NOW; t("clima agora", [] { weatherDrawNow(); });
    simScreen = SCR_PC; t("pc", [] { pcDraw(); });
    fflush(stdout); _exit(0);
  }
  auto want = [&](const char *n) { return only.empty() || strstr(n, only.c_str()); };
  char name[64];

  // Clima
  weatherWork();
  for(int st = 0; st < CLK_COUNT; st++)
  {
    simMillis += 2345;
    feat.cStyle = st; snprintf(name, sizeof(name), "clock_%d_%s", st, clockStyleName(st));
    for(char *c = name; *c; c++) if(*c == ' ' || (unsigned char)*c > 127) *c = '_';
    if(want(name)) { simScreen = SCR_CLOCK; clockDraw(); save(name); }
  }
  if(want("wx_now")) { simScreen = SCR_WX_NOW; feat.wNowStyle = 0; weatherDrawNow(); save("wx_now_real"); feat.wNowStyle = 1; weatherDrawNow(); save("wx_now_simple"); feat.wNowStyle = 0; }
  if(want("wx_hours")) { weatherDrawHours(); save("wx_hours"); simMillis += 3000; for(int st = 1; st <= 2; st++) { feat.wHoursStyle = st; weatherDrawHours(); char nm[24]; snprintf(nm, sizeof(nm), "wx_hours_%d", st); save(nm); } feat.wHoursStyle = 0; }
  if(want("intro"))
  {
    strlcpy(feat.place, "London Heathrow Airport", sizeof(feat.place));
    for(int k = 0; k < 3; k++)
    {
      weatherOpen(); simMillis += 150 + k * 200;
      weatherDrawNow(); snprintf(name, sizeof(name), "intro_now_%d", k); save(name);
      weatherOpen(); simMillis += 150 + k * 200;
      weatherDrawHours(); snprintf(name, sizeof(name), "intro_hours_%d", k); save(name);
      weatherOpen(); simMillis += 150 + k * 200;
      weatherDrawDays(); snprintf(name, sizeof(name), "intro_days_%d", k); save(name);
    }
    strlcpy(feat.place, "London", sizeof(feat.place));
  }
  if(want("wx_days")) { weatherDrawDays(); save("wx_days"); simMillis += 3000; feat.wDaysStyle = 1; weatherDrawDays(); save("wx_days_1"); feat.wDaysStyle = 0; }

  // Cenas do ceu no relogio realista (troca a hora e o clima)
  if(want("scene"))
  {
    feat.cStyle = CLK_REAL;
    const struct { int h, m; const char *n; } T[] = {{5, 52, "amanhecer"}, {14, 20, "tarde"}, {18, 30, "entardecer"}, {23, 7, "noite"}};
    for(auto &t : T) { simNow = local(2026, 11, 13, t.h, t.m, 15); simMillis += 1234; clockDraw(); snprintf(name, sizeof(name), "scene_%s", t.n); save(name); }
    simNow = local(2026, 11, 13, 10, 32, 15);
  }

  // Voos
  if(want("flights"))
  {
    simScreen = SCR_FLIGHTS; flightsOpen(); flightsWork();
    feat.fColorBy = 0; flightsDraw(); save("flights_alt");
    feat.fColorBy = 1; flightsDraw(); save("flights_cat");
    feat.fIcon = 1; flightsDraw(); save("flights_arrow"); feat.fIcon = 0;
    for(int z = 0; z < 4; z++) { feat.fSize = z; char nm[32]; snprintf(nm, sizeof(nm), "flights_tam%d", z); flightsDraw(); save(nm); }
    feat.fSize = 1; feat.fColorBy = 2; feat.fColor = 0xFF5FD2; flightsDraw(); save("flights_cor"); feat.fColorBy = 0;
    flightsLongPress(); flightsWork(); flightsDraw(); save("flights_detail");
    flightsShortPress(); flightsWork(); flightsDraw(); save("flights_detail2");
    flightsClose();
  }


  // Wi-Fi explorer
  if(want("wifi"))
  {
    feat.scrWifi = true; simScreen = SCR_WIFI; wifiOpen();
    wifiDraw(); save("wifi_lista");
    wifiShortPress(); wifiDraw(); save("wifi_canais");
    wifiShortPress();
  }

  // Cotacoes
  if(want("quotes"))
  {
    auto rd = [](const char *f) { std::ifstream in(simDir + "/data/" + f, std::ios::binary); std::stringstream ss; ss << in.rdbuf(); return ss.str(); };
    overrideData["json/last"] = rd("quotes_last.json");
    overrideData["json/daily"] = rd("quotes_daily.json");
    feat.scrQuotes = true; quotesBegin(); simScreen = SCR_QUOTES; quotesOpen();
    for(int i = 0; i < 4; i++) quotesWork();
    feat.qStyle = 0; quotesDraw(); save("quotes_rodizio");
    simMillis += 7000; quotesTick(); quotesDraw(); save("quotes_rodizio2");
    feat.qStyle = 1; quotesDraw(); save("quotes_placa3");
    const char *more[] = {"ETH", "GBP", "ARS", "JPY", "DOGE"};
    for(auto m : more) strlcpy(feat.qItems[feat.qCount++], m, 8);
    featGen++; quotesWork(); quotesDraw(); save("quotes_placa8");
    overrideData.erase("json/last"); overrideData.erase("json/daily");
  }

  // Porta-retrato
  if(want("photo"))
  {
    simScreen = SCR_PHOTOS; feat.pFade = false;
    for(int st = 0; st < PCLK_COUNT; st++)
    {
      feat.pStyle = st; photosOpen(); feat.pWxPop = false; photosDraw();
      snprintf(name, sizeof(name), "photo_%d", st); save(name);
    }
    feat.pStyle = PCLK_FOOTER; feat.pWxPop = true; photosDraw(); save("photo_pop");
  }

  // Horoscopo
  if(want("horo"))
  {
    simScreen = SCR_HORO;
    for(int th = 0; th < 3; th++)
      for(int sg : {4, 7})
      {
        feat.hTheme = th; feat.hSigns = 1 << sg; horoOpen(); horoDraw();
        snprintf(name, sizeof(name), "horo_%d_%d", th, sg); save(name);
      }
  }
  // Lua ao longo da noite e animacao do ceu
  if(want("moon"))
  {
    feat.cStyle = CLK_REAL; simScreen = SCR_CLOCK;
    const struct { int d, h, m; } T[] = {{13, 19, 30}, {13, 23, 0}, {14, 3, 0}, {20, 21, 0}, {24, 1, 0}};
    for(auto &t : T)
    {
      simNow = local(2026, 11, t.d, t.h, t.m, 0);
      // tempo limpo de noite
      clockDraw(); snprintf(name, sizeof(name), "moon_%02d_%02dh", t.d, t.h); save(name);
    }
    simNow = local(2026, 11, 13, 10, 32, 15);
  }
  if(want("rcard"))
  {
    simScreen = SCR_WX_RADAR; feat.wCards = true;
    for(int light = 0; light < 2; light++)
    {
      fakeRadar(light);
      for(int st = 0; st <= 10; st++) { feat.wCardStyle = st; simMillis += 777; weatherDrawRadar(); snprintf(name, sizeof(name), "rcard_%s_%02d", light ? "claro" : "escuro", st); save(name); }
    }
    feat.wCardStyle = 0;
  }
  if(want("lua"))
  {
    simScreen = SCR_MOON;
    const struct { int d, h; } T[] = {{13, 19}, {17, 20}, {24, 22}, {28, 21}};
    for(auto &t : T) { simNow = local(2026, 11, t.d, t.h, 0, 0); simMillis += 500; moonDraw(); snprintf(name, sizeof(name), "lua_%02d", t.d); save(name); }
    const double lat0 = feat.lat; feat.lat = 48.85; feat.lon = 2.35; simNow = local(2026, 11, 17, 20, 0, 0); moonDraw(); save("lua_17_norte"); feat.lat = lat0; feat.lon = -0.1278;
    simNow = local(2026, 11, 13, 10, 32, 15);
  }
  if(want("catalogo"))
  {
    simScreen = SCR_WX_RADAR; feat.wCards = true; feat.wCardStyle = 0;
    weatherSimRadar(fakeMap(false, true, false), VIEW_DARK); simMillis += 500; weatherDrawRadar(); save("cat_radar_escuro");
    weatherSimRadar(fakeMap(true, true, false), VIEW_BRIGHT); simMillis += 500; weatherDrawRadar(); save("cat_radar_claro");
    weatherSimRadar(fakeMap(false, true, true), VIEW_PLAIN); simMillis += 500; weatherDrawRadar(); save("cat_radar_semmapa");
    simScreen = SCR_FLIGHTS; flightsOpen(); flightsWork(); feat.fColorBy = 0; feat.fIcon = 0;
    feat.fStyle = VIEW_DARK; flightsSimMap(fakeMap(false, false, false), VIEW_DARK); flightsDraw(); save("cat_voos_escuro");
    feat.fStyle = VIEW_BRIGHT; flightsSimMap(fakeMap(true, false, false), VIEW_BRIGHT); flightsDraw(); save("cat_voos_claro");
    feat.fStyle = VIEW_PLAIN; flightsDraw(); save("cat_voos_sonar");
    feat.fStyle = VIEW_DARK; flightsSimMap(fakeMap(false, false, false), VIEW_DARK); feat.fIcon = 1; flightsDraw(); save("cat_voos_seta"); feat.fIcon = 0;
    flightsClose();
  }
  if(want("pc"))
  {
    feat.scrPc = true; strlcpy(feat.pcHost, "192.168.0.10", sizeof(feat.pcHost));
    simScreen = SCR_PC; pcOpen();
    pcDraw(); save("pc_vazio");
    for(int k = 0; k < 34; k++) { overrideData["data.json"] = lhmJson(k); simMillis += 2001; pcWork(); }
    simMillis += 900;
    for(int st = 0; st < 3; st++) { feat.pcStyle = st; pcDraw(); char nm[24]; snprintf(nm, sizeof(nm), "pc_%d", st); save(nm); }
    overrideData.erase("data.json"); overrideFail["data.json"] = -1;
    for(int k = 0; k < 3; k++) { simMillis += 2001; pcWork(); }
    pcDraw(); save("pc_desligado");
    overrideFail.clear(); pcClose(); feat.pcStyle = 0; feat.scrPc = false;
  }
  if(want("anim"))
  {
    simScreen = SCR_WX_NOW; feat.wNowStyle = 0;
    for(int k = 0; k < 3; k++) { simMillis += 7000; weatherDrawNow(); snprintf(name, sizeof(name), "anim_%d", k); save(name); }
  }

  // Noticias e DMR
  if(want("news"))
  {
    simScreen = SCR_NEWS; newsOpen(); newsWork();
    feat.nStyle = 0; newsDraw(); save("news_aurora"); feat.nStyle = 1; newsDraw(); save("news_simple"); feat.nStyle = 0;
    feat.nUrgent = false; newsOpen();
    for(int st = 2; st <= 5; st++) { feat.nStyle = st; simMillis += 1700; newsDraw(); snprintf(name, sizeof(name), "news_tv_%d", st); save(name); }
    feat.nUrgent = true; feat.nStyle = 2; newsDraw(); save("news_tv_2b");
    feat.nStyle = 0;
  }
  if(want("dmr"))
  {
    simScreen = SCR_DMR; dmrOpen(); dmrWork(); dmrOpen(); dmrWork(); dmrOpen(); dmrWork();
    feat.dStyle = 0; dmrDraw(); save("dmr_aurora"); dmrLongPress(); dmrDraw(); save("dmr_aurora_list"); dmrLongPress();
    feat.dStyle = 1; dmrDraw(); save("dmr_simple");
    struct P { const char *call, *fname, *city, *state, *country; long id; } ps[] = {
      {"PY2ABC", "Carlos", "Sao Paulo", "Sao Paulo", "Brazil", 7241234}, {"CT1XYZ", "Joao Pereira", "Lisboa", "", "Portugal", 2680123},
      {"JA1ABC", "Takeshi", "Tokyo", "", "Japan", 4401234}, {"W1AW", "Hiram", "Newington", "Connecticut", "United States", 3100001},
      {"DL1ABC", "Klaus", "Berlin", "", "Germany", 2621234}, {"LU1ABC", "Martin", "Buenos Aires", "", "Argentina", 7221234}};
    auto lh = [&](const char *first, bool live) {
      std::string h = "<table>";
      const char *calls[] = {first, "PY2ABC", "CT1XYZ", "JA1ABC", "W1AW", "DL1ABC", "LU1ABC"};
      for(int i = 0; i < 7; i++)
        h += std::string("<tr><td>10:1") + std::to_string(i) + ":01</td><td>DMR Slot 2</td><td>" + calls[i] + "</td><td>TG 724</td><td>Net</td><td>" + (i == 0 && live ? "TX" : "12.4") + "</td><td>0%</td></tr>";
      return h + "</table>";
    };
    for(auto &p : ps)
    {
      char js[400]; snprintf(js, sizeof(js), "{\"count\":1,\"results\":[{\"callsign\":\"%s\",\"fname\":\"%s\",\"city\":\"%s\",\"state\":\"%s\",\"country\":\"%s\",\"id\":%ld}]}", p.call, p.fname, p.city, p.state, p.country, p.id);
      overrideData[std::string("callsign=") + p.call] = js;
    }
    const char *nm[] = {"", "", "fundo", "faixa", "pixel", "bordas"};
    for(int st = 2; st <= 5; st++)
      for(int k = 0; k < 6; k += (st == 2 ? 1 : 2))
      {
        overrideData["lh.php"] = lh(ps[k].call, k % 2 == 0);
        for(int r = 0; r < 9; r++) { dmrOpen(); dmrWork(); }
        feat.dStyle = st; simMillis += 41000; dmrDraw();
        char fn[48]; snprintf(fn, sizeof(fn), "dmr_%s_%s", nm[st], ps[k].call); save(fn);
        if(k == 0) { dmrLongPress(); dmrDraw(); snprintf(fn, sizeof(fn), "dmr_%s_lista", nm[st]); save(fn); dmrLongPress(); }
      }
    for(auto &p : ps) overrideData.erase(std::string("callsign=") + p.call);
    overrideData.erase("lh.php");
    feat.dStyle = 0; dmrDraw(); save("dmr_aurora_flag");
  }
  fflush(stdout);
  _exit(0);   // sem destrutores globais (fontes na flash)
}
