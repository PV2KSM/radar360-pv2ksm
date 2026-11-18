#include "Flights.h"
#include "Lang.h"
#include "Gfx.h"
#include "Feat.h"
#include "Http.h"
#include "Json.h"
#include "MapImg.h"
#include "Net.h"
#include "Ui.h"
#include "Crash.h"
#include "Pack.h"
#include <math.h>

#define MAX_PLANES 40
static const int R_PX = 160;   // raio do radar na tela

struct Plane
{
  char hex[8], call[10], type[6], reg[12], sq[6];
  double lat, lon;
  int32_t alt;      // pes (-1 = no solo)
  float gs, track;  // nos, graus
  int16_t vrate;    // pes/min
  float distKm, brg;
  int8_t cat;       // AcCat
  double posAt;     // quando a posicao foi medida (epoch, s)
  uint8_t miss;     // buscas seguidas sem aparecer (modo "as duas")
  uint8_t src;      // 1 = adsb.fi, 2 = adsb.lol
};

// saude das fontes (modo "as duas" alterna entre elas)
static const char *const SRC_NAME[2] = {"adsb.fi", "adsb.lol"};
static uint32_t srcOkAt[2] = {0, 0};
static char srcErr[2][40] = {"", ""};
static uint8_t nextSrc = 0;

static SemaphoreHandle_t mtx;
static Plane planes[MAX_PLANES];
static int planeCount = 0;
static uint32_t lastFetch = 0;
static time_t dataTime = 0;
static char status[64] = "Aguardando dados…";

static uint16_t *mapImg = nullptr;     // 360x360 pronto
static volatile bool mapReady = false;
static uint32_t mapGen = 0;            // featGen usado no mapa atual
static volatile uint8_t mapView = VIEW_DARK;   // estilo (ja resolvido) do mapa atual
static uint32_t mapTryAt = 0;

static bool inDetail = false;
static char selHex[8] = "";
static volatile bool opened = false;

// Rota (adsbdb)
struct Route { char call[10]; char from[8], to[8], fromCity[28], toCity[28], airline[32]; bool ok; uint32_t at; };
static Route routes[8];
static int routeNext = 0;
static char routeWanted[10] = "";

static void lock() { xSemaphoreTake(mtx, portMAX_DELAY); }
static void unlock() { xSemaphoreGive(mtx); }

// ---- Nomes de modelos (codigos ICAO mais comuns) ----
struct TypeName { const char *code, *name; };
static const TypeName TYPES[] = {
  {"A19N","Airbus A319neo"},{"A20N","Airbus A320neo"},{"A21N","Airbus A321neo"},{"A318","Airbus A318"},{"A319","Airbus A319"},
  {"A320","Airbus A320"},{"A321","Airbus A321"},{"A332","Airbus A330-200"},{"A333","Airbus A330-300"},{"A338","Airbus A330-800"},
  {"A339","Airbus A330-900"},{"A343","Airbus A340-300"},{"A346","Airbus A340-600"},{"A359","Airbus A350-900"},{"A35K","Airbus A350-1000"},
  {"A388","Airbus A380"},{"BCS1","Airbus A220-100"},{"BCS3","Airbus A220-300"},{"A400","Airbus A400M"},
  {"B712","Boeing 717"},{"B733","Boeing 737-300"},{"B734","Boeing 737-400"},{"B735","Boeing 737-500"},{"B736","Boeing 737-600"},
  {"B737","Boeing 737-700"},{"B738","Boeing 737-800"},{"B739","Boeing 737-900"},{"B37M","Boeing 737 MAX 7"},{"B38M","Boeing 737 MAX 8"},
  {"B39M","Boeing 737 MAX 9"},{"B3XM","Boeing 737 MAX 10"},{"B744","Boeing 747-400"},{"B748","Boeing 747-8"},{"B752","Boeing 757-200"},
  {"B753","Boeing 757-300"},{"B762","Boeing 767-200"},{"B763","Boeing 767-300"},{"B764","Boeing 767-400"},{"B772","Boeing 777-200"},
  {"B77L","Boeing 777-200LR"},{"B773","Boeing 777-300"},{"B77W","Boeing 777-300ER"},{"B778","Boeing 777-8"},{"B779","Boeing 777-9"},
  {"B788","Boeing 787-8"},{"B789","Boeing 787-9"},{"B78X","Boeing 787-10"},{"E170","Embraer 170"},{"E175","Embraer 175"},
  {"E190","Embraer 190"},{"E195","Embraer 195"},{"E290","Embraer E190-E2"},{"E295","Embraer E195-E2"},{"E145","Embraer ERJ-145"},
  {"E135","Embraer ERJ-135"},{"E50P","Embraer Phenom 100"},{"E55P","Embraer Phenom 300"},{"E545","Embraer Legacy 450"},{"E550","Embraer Legacy 500"},
  {"E35L","Embraer Legacy 600"},{"E120","Embraer Brasília"},{"E110","Embraer Bandeirante"},{"E314","Embraer Super Tucano"},{"E2RR","Embraer C-390"},
  {"C390","Embraer C-390"},{"AT43","ATR 42-300"},{"AT45","ATR 42-500"},{"AT46","ATR 42-600"},{"AT72","ATR 72"},{"AT75","ATR 72-500"},
  {"AT76","ATR 72-600"},{"DH8D","Dash 8 Q400"},{"CRJ2","Bombardier CRJ200"},{"CRJ7","Bombardier CRJ700"},{"CRJ9","Bombardier CRJ900"},
  {"CL30","Challenger 300"},{"CL35","Challenger 350"},{"CL60","Challenger 600"},{"GL5T","Global 5000"},{"GLEX","Global Express"},
  {"GL7T","Global 7500"},{"GLF4","Gulfstream IV"},{"GLF5","Gulfstream V"},{"GLF6","Gulfstream G650"},{"G280","Gulfstream G280"},
  {"LJ35","Learjet 35"},{"LJ45","Learjet 45"},{"LJ60","Learjet 60"},{"LJ75","Learjet 75"},{"C25A","Citation CJ2"},{"C25B","Citation CJ3"},
  {"C25C","Citation CJ4"},{"C510","Citation Mustang"},{"C525","CitationJet"},{"C550","Citation II"},{"C560","Citation V"},{"C56X","Citation Excel"},
  {"C680","Citation Sovereign"},{"C68A","Citation Latitude"},{"C700","Citation Longitude"},{"F2TH","Falcon 2000"},{"F900","Falcon 900"},
  {"FA7X","Falcon 7X"},{"FA8X","Falcon 8X"},{"H25B","Hawker 800"},{"PC12","Pilatus PC-12"},{"PC24","Pilatus PC-24"},{"C208","Cessna Caravan"},
  {"C172","Cessna 172"},{"C182","Cessna 182"},{"C152","Cessna 152"},{"C210","Cessna 210"},{"P28A","Piper Cherokee"},{"PA34","Piper Seneca"},
  {"PA46","Piper Malibu"},{"BE20","King Air 200"},{"BE30","King Air 300"},{"BE35","Bonanza"},{"BE36","Bonanza A36"},{"BE58","Baron"},
  {"B350","King Air 350"},{"BE9L","King Air 90"},{"TBM7","TBM 700"},{"TBM8","TBM 850"},{"TBM9","TBM 900"},{"SR22","Cirrus SR22"},
  {"SF50","Cirrus Vision Jet"},{"DA42","Diamond DA42"},{"DA40","Diamond DA40"},{"AS50","Helibras Esquilo"},{"EC35","Airbus H135"},
  {"EC45","Airbus H145"},{"EC30","Airbus H130"},{"EC55","Airbus H155"},{"EC25","Airbus H225"},{"AS65","Dauphin"},{"S76","Sikorsky S-76"},
  {"R44","Robinson R44"},{"R66","Robinson R66"},{"B06","Bell 206"},{"B407","Bell 407"},{"B412","Bell 412"},{"B429","Bell 429"},
  {"A109","AW109"},{"A139","AW139"},{"A169","AW169"},{"H60","Black Hawk"},{"C130","Hercules C-130"},{"C30J","Hercules C-130J"},
  {"K35R","KC-135"},{"A30B","Airbus A300"},{"A306","Airbus A300-600"},{"A310","Airbus A310"},{"MD11","MD-11"},{"B190","Beech 1900"},
  {"SW4","Metro"},{"JS41","Jetstream 41"},{"D328","Dornier 328"},{"F100","Fokker 100"},{"F70","Fokker 70"},{"AN12","Antonov An-12"},
  {"AN26","Antonov An-26"},{"A124","Antonov An-124"},{"IL76","Ilyushin Il-76"},{"GA8","Airvan"},
};

static const char *typeName(const char *code)
{
  for(const auto &t : TYPES) if(!strcmp(t.code, code)) return t.name;
  return code;
}

// ---- Categorias (silhuetas) ----
enum AcCat : int8_t { CAT_WIDE = 0, CAT_NARROW, CAT_REGIONAL, CAT_TURBOPROP, CAT_BIZJET, CAT_LIGHT, CAT_HELI };
static const char *const CAT_NAMES[3][7] = {
  {"Widebody","Narrowbody","Regional","Turboprop","Business jet","Light","Helicopter"},
  {"Widebody","Narrowbody","Regional","Turboélice","Executivo","Leve","Helicóptero"},
  {"Fuselaje ancho","Fuselaje estrecho","Regional","Turbohélice","Ejecutivo","Ligero","Helicóptero"}};
static const uint32_t CAT_COL[] = {0xFF8FB1u, 0x5FFFD2u, 0x7FD8FFu, 0xFFC857u, 0xC7A6FFu, 0xB8F27Cu, 0xFF9F5Au};
static const uint32_t CAT_COL_BRIGHT[] = {0xC2185Bu, 0x00806Au, 0x1F5FBFu, 0xB86200u, 0x6A3FC2u, 0x4E8A12u, 0xC2550Au};

struct CatRule { const char *p; int8_t c; };
// Primeira regra que casa vence (exatas antes dos prefixos curtos)
static const CatRule RULES[] = {
  {"A318",1},{"A319",1},{"A320",1},{"A321",1},{"A19N",1},{"A20N",1},{"A21N",1},{"A124",0},{"A400",3},{"A30",0},{"A310",0},
  {"A33",0},{"A34",0},{"A35",0},{"A38",0},{"A1",6},{"AW",6},{"AS3",6},{"AS5",6},{"AS6",6},{"AT",3},{"AN12",3},{"AN26",3},{"AA5",5},
  {"B37M",1},{"B38M",1},{"B39M",1},{"B3XM",1},{"B71",1},{"B72",1},{"B73",1},{"B75",1},{"B74",0},{"B76",0},{"B77",0},{"B78",0},
  {"B06",6},{"B407",6},{"B412",6},{"B429",6},{"B505",6},{"BK17",6},{"B350",3},{"B190",3},{"BE20",3},{"BE30",3},{"BE9",3},{"BE",5},
  {"BCS",1},{"BA46",2},{"C919",1},{"C17",0},{"C130",3},{"C30J",3},{"C208",3},{"C212",3},{"C295",3},{"CN35",3},{"C25",4},{"C5",4},
  {"C6",4},{"C7",4},{"C1",5},{"C2",5},{"C3",5},{"C4",5},{"CRJ",2},{"CL",4},{"D328",3},{"DH8",3},{"DHC",3},{"DA4",5},{"DA2",5},
  {"E35L",4},{"E5",4},{"E110",3},{"E120",3},{"E314",3},{"E1",2},{"E2",2},{"E7",2},{"EC",6},{"EA50",4},{"F100",2},{"F70",2},{"F2TH",4},
  {"F900",4},{"FA",4},{"G2",4},{"G1",4},{"GL",4},{"H25",4},{"H60",6},{"H1",6},{"HDJT",4},{"IL",0},{"JS4",3},{"L410",3},{"LJ",4},
  {"M20",5},{"MD11",0},{"MD8",1},{"MD9",1},{"MI",6},{"P46T",3},{"P28",5},{"PA",5},{"PC12",3},{"PC24",4},{"PC",3},{"PRM1",4},
  {"R22",6},{"R44",6},{"R66",6},{"RJ",2},{"RV",5},{"S76",6},{"S92",6},{"SF50",4},{"SF34",3},{"SR2",5},{"SU95",2},{"SW4",3},{"TBM",3},{"UH1",6},
};

static int8_t acCat(const char *type, const char *adsb, int32_t alt, float gs)
{
  if(type && type[0])
    for(const auto &r : RULES) if(!strncmp(type, r.p, strlen(r.p))) return r.c;
  if(adsb && adsb[0] == 'A')
    switch(adsb[1]) { case '7': return CAT_HELI; case '5': return CAT_WIDE; case '4': case '3': return CAT_NARROW; case '2': return CAT_BIZJET; case '1': return CAT_LIGHT; }
  if(adsb && adsb[0] == 'B') return CAT_LIGHT;   // planadores, balões
  if(alt >= 0 && alt < 10000 && gs > 0 && gs < 160) return CAT_LIGHT;
  return CAT_NARROW;
}

// ---- Unidades ----
static void fmtAlt(char *b, size_t n, int32_t ft)
{
  if(ft < 0) { snprintf(b, n, T("ground","solo","en tierra")); return; }
  if(feat.fAltU == 0) { if(ft >= 18000) snprintf(b, n, "FL%03ld", (long)((ft + 50) / 100)); else snprintf(b, n, "%ld ft", (long)ft); }
  else
  {
    long m = lround(ft * 0.3048);
    if(m >= 1000) snprintf(b, n, "%ld.%03ld m", m / 1000, m % 1000); else snprintf(b, n, "%ld m", m);
  }
}
static void fmtSpeed(char *b, size_t n, float kt)
{
  if(feat.fSpdU == 0) snprintf(b, n, "%d kt", (int)lroundf(kt));
  else if(feat.fSpdU == 2) snprintf(b, n, "%d mph", (int)lroundf(kt * 1.15078f));
  else snprintf(b, n, "%d km/h", (int)lroundf(kt * 1.852f));
}
static void fmtDist(char *b, size_t n, float km)
{
  const float v = feat.fDistU == 0 ? km / 1.852f : feat.fDistU == 2 ? km / 1.609344f : km;
  const char *u = feat.fDistU == 0 ? "nm" : feat.fDistU == 2 ? "mi" : "km";
  snprintf(b, n, v < 10 ? "%.1f %s" : "%.0f %s", v, u);
}
static const char *compass(float deg)
{
  static const char *const N[] = {"N", "NE", "L", "SE", "S", "SO", "O", "NO"};
  return N[((int)lroundf(deg / 45.0f)) & 7];
}

// ---- Geometria ----
static void distBrg(double lat0, double lon0, double lat1, double lon1, float &km, float &brg)
{
  const double p0 = lat0 * M_PI / 180, p1 = lat1 * M_PI / 180, dl = (lon1 - lon0) * M_PI / 180;
  const double x = dl * cos((p0 + p1) / 2), y = p1 - p0;
  km = (float)(sqrt(x * x + y * y) * 6371.0);
  double b = atan2(sin(dl) * cos(p1), cos(p0) * sin(p1) - sin(p0) * cos(p1) * cos(dl)) * 180 / M_PI;
  if(b < 0) b += 360;
  brg = (float)b;
}

// ---- Rede ----
static void parse(HttpBuf &buf, int src, bool merge)
{
  JsonDocument filter;
  JsonObject f = filter["ac"][0].to<JsonObject>();
  for(const char *k : {"hex", "flight", "r", "t", "lat", "lon", "alt_baro", "gs", "track", "baro_rate", "squawk", "category", "seen_pos"}) f[k] = true;
  filter["aircraft"][0] = f;
  filter["now"] = true;
  JsonDocument doc(SpiRamAllocator::instance());
  if(deserializeJson(doc, buf.str(), buf.len, DeserializationOption::Filter(filter)))
  {
    lock(); strlcpy(status, T("Invalid response","Resposta inválida","Respuesta inválida"), sizeof(status)); unlock();
    return;
  }
  double cLat, cLon; featFlightsCenter(cLat, cLon, nullptr);
  JsonArrayConst arr = doc["ac"].is<JsonArrayConst>() ? doc["ac"].as<JsonArrayConst>() : doc["aircraft"].as<JsonArrayConst>();
  double now = doc["now"] | 0.0;
  if(now > 1e11) now /= 1000.0;                 // uma das APIs manda em milissegundos
  if(now < 1.7e9) now = (double)time(nullptr);
  static Plane tmp[MAX_PLANES * 2];
  int n = 0;
  for(JsonObjectConst a : arr)
  {
    if(!a["lat"].is<double>() || !a["lon"].is<double>()) continue;
    Plane p; memset(&p, 0, sizeof(p));
    strlcpy(p.hex, a["hex"] | "", sizeof(p.hex));
    strlcpy(p.call, a["flight"] | "", sizeof(p.call));
    for(int i = strlen(p.call) - 1; i >= 0 && p.call[i] == ' '; i--) p.call[i] = 0;
    strlcpy(p.reg, a["r"] | "", sizeof(p.reg));
    strlcpy(p.type, a["t"] | "", sizeof(p.type));
    strlcpy(p.sq, a["squawk"] | "", sizeof(p.sq));
    p.lat = a["lat"]; p.lon = a["lon"];
    if(a["alt_baro"].is<int>()) p.alt = a["alt_baro"]; else p.alt = -1;
    p.gs = a["gs"] | 0.0f; p.track = a["track"] | 0.0f; p.vrate = a["baro_rate"] | 0;
    if(p.alt < 0 && !feat.fGround) continue;
    if(p.alt >= 0 && (p.alt < feat.fMinAlt || p.alt > feat.fMaxAlt)) continue;
    p.cat = acCat(p.type, a["category"] | "", p.alt, p.gs);
    p.posAt = now - (a["seen_pos"] | 0.0);
    p.src = 1 << src;
    if(!(feat.fTypes & (1 << p.cat))) continue;
    distBrg(cLat, cLon, p.lat, p.lon, p.distKm, p.brg);
    if(p.distKm > feat.fRadiusKm * 1.5f) continue;
    if(n < MAX_PLANES * 2) tmp[n++] = p;
    else
    {
      int far = 0; for(int i = 1; i < n; i++) if(tmp[i].distKm > tmp[far].distKm) far = i;
      if(p.distKm < tmp[far].distKm) tmp[far] = p;
    }
  }
  lock();
  static uint32_t parseGen = 0;
  if(parseGen != featGen) { parseGen = featGen; planeCount = 0; }   // centro/filtros mudaram: nao junta com o antigo
  if(merge)
  {
    // junta com o que ja havia (da outra fonte): o mesmo aviao (hex) fica com a posicao mais recente;
    // quem nao apareceu nesta busca so sai depois de 3 buscas seguidas sem aparecer em nenhuma
    for(int i = 0; i < planeCount; i++) planes[i].miss++;
    for(int k = 0; k < n; k++)
    {
      int j = -1;
      for(int i = 0; i < planeCount; i++) if(!strcmp(planes[i].hex, tmp[k].hex)) { j = i; break; }
      if(j < 0) continue;
      const uint8_t srcs = planes[j].src | tmp[k].src;
      if(tmp[k].posAt >= planes[j].posAt) planes[j] = tmp[k];
      else if(!planes[j].call[0] && tmp[k].call[0]) strlcpy(planes[j].call, tmp[k].call, sizeof(planes[j].call));
      planes[j].miss = 0; planes[j].src = srcs;
      tmp[k].hex[0] = 0;   // ja usado
    }
    int m = 0;
    for(int i = 0; i < planeCount; i++) if(planes[i].miss < 3) planes[m++] = planes[i];
    for(int k = 0; k < n && m < MAX_PLANES; k++) if(tmp[k].hex[0]) planes[m++] = tmp[k];
    // posicao/distancia de quem ficou de outra busca continua valida (mesmo centro)
    std::sort(planes, planes + m, [](const Plane &a, const Plane &b) { return a.distKm < b.distKm; });
    planeCount = min(m, (int)feat.fMax);
  }
  else
  {
    std::sort(tmp, tmp + n, [](const Plane &a, const Plane &b) { return a.distKm < b.distKm; });
    const int keep = min(n, (int)feat.fMax);
    memcpy(planes, tmp, sizeof(Plane) * keep);
    planeCount = keep;
  }
  const int keep = planeCount;
  dataTime = time(nullptr);
  if(keep) status[0] = 0; else strlcpy(status, T("No planes in range","Nenhum avião no raio","Sin aviones en el rango"), sizeof(status));
  unlock();
}

static bool fetchFrom(int src, bool merge)
{
  char url[160];
  double cLat, cLon; featFlightsCenter(cLat, cLon, nullptr);
  const int nm = (int)ceil(feat.fRadiusKm * 1.5 / 1.852);
  if(src == 1) snprintf(url, sizeof(url), "https://api.adsb.lol/v2/point/%.5f/%.5f/%d", cLat, cLon, min(nm, 250));
  else snprintf(url, sizeof(url), "https://opendata.adsb.fi/api/v3/lat/%.5f/lon/%.5f/dist/%d", cLat, cLon, min(nm, 250));
  HttpBuf buf; char err[48];
  crumbWorker(src ? "voos:baixar lol" : "voos:baixar fi");
  const int code = httpGet(url, buf, 640 * 1024, err, sizeof(err), "application/json");
  crumbWorker("voos:json");
  const bool ok = code == 200;
  if(ok) { parse(buf, src, merge); srcOkAt[src] = millis(); srcErr[src][0] = 0; }
  else
  {
    strlcpy(srcErr[src], err, sizeof(srcErr[src]));
    lock();
    if(code == -106) strlcpy(status, T("Searching planes…","Buscando aviões…","Buscando aviones…"), sizeof(status));   // memoria ainda ocupada: tenta no proximo ciclo
    else if(!planeCount) snprintf(status, sizeof(status), T("Flights: %s","Voos: %s","Vuelos: %s"), err);
    unlock();
  }
  crumbWorker("voos:ok");
  buf.release();
  return ok;
}

static void fetchFlights()
{
  if(feat.fSource != 2) { fetchFrom(feat.fSource, false); return; }
  // as duas: alterna; se a da vez falhar, tenta a outra na mesma rodada
  const int s = nextSrc; nextSrc ^= 1;
  if(!fetchFrom(s, true)) fetchFrom(s ^ 1, true);
}

const char *flightsSources(char *b, size_t n)
{
  b[0] = 0;
  for(int s = 0; s < 2; s++)
  {
    if(feat.fSource != 2 && feat.fSource != s) continue;
    const char *st = srcErr[s][0] ? srcErr[s] : srcOkAt[s] ? "ok" : "aguardando";
    char part[64]; snprintf(part, sizeof(part), "%s%s %s", b[0] ? " · " : "", SRC_NAME[s], st);
    strlcat(b, part, n);
  }
  return b;
}

static void fetchRoute(const char *call)
{
  char url[96]; snprintf(url, sizeof(url), "https://api.adsbdb.com/v0/callsign/%s", call);
  HttpBuf buf; char err[40];
  Route r; memset(&r, 0, sizeof(r)); strlcpy(r.call, call, sizeof(r.call)); r.at = millis();
  if(httpGet(url, buf, 16 * 1024, err, sizeof(err), "application/json") == 200)
  {
    JsonDocument doc;
    if(!deserializeJson(doc, buf.str(), buf.len))
    {
      JsonObjectConst fr = doc["response"]["flightroute"];
      if(!fr.isNull())
      {
        strlcpy(r.from, fr["origin"]["iata_code"] | "", sizeof(r.from));
        strlcpy(r.to, fr["destination"]["iata_code"] | "", sizeof(r.to));
        strlcpy(r.fromCity, fr["origin"]["municipality"] | "", sizeof(r.fromCity));
        strlcpy(r.toCity, fr["destination"]["municipality"] | "", sizeof(r.toCity));
        strlcpy(r.airline, fr["airline"]["name"] | "", sizeof(r.airline));
        r.ok = r.from[0] && r.to[0];
      }
    }
  }
  buf.release();
  lock(); routes[routeNext] = r; routeNext = (routeNext + 1) % 8; unlock();
}

static void buildMap()
{
  const uint32_t gen = featGen;
  double cLat, cLon; featFlightsCenter(cLat, cLon, nullptr);
  const uint8_t view = featViewNow(feat.fStyle, cLat, cLon);
  const uint8_t style = featMapStyle(view);
  if(style == MAP_NONE) { mapReady = false; mapGen = gen; mapView = view; return; }
  if(!mapImg) mapImg = (uint16_t *)heap_caps_malloc(LCD_W * LCD_H * 2, MALLOC_CAP_SPIRAM);
  if(!mapImg) return;
  MapCanvas mc; char err[64];
  const double mpp = feat.fRadiusKm * 1000.0 / R_PX;
  // 1) Cache da imagem final no flash: mesmo local/zoom/estilo -> carrega na hora, sem baixar nada
  if(mapBaseLoad(cLat, cLon, mpp, style, view, mapImg))
  {
    mapView = view; mapReady = true; mapGen = gen;
    lock(); status[0] = 0; unlock();
    return;
  }
  crumbWorker("voos:mapa");
  if(mapPrepare(mc, cLat, cLon, mpp, style, err, sizeof(err)))
  {
    mapReady = false;
    crumbWorker("voos:mapa-reduz");
    mapDownsample(mc, (const uint16_t *)mc.big->getBuffer(), mapImg);
    // Radar escuro com OSM: escurece; mapa claro: apaga um pouco para os avioes aparecerem
    if(featMapNeedsDim(view)) mapTint(mapImg, 90, 0x00140Fu);
    else if(view == VIEW_BRIGHT) mapTint(mapImg, 200, 0);
    mapView = view;
    mapReady = true;
    mapGen = gen;
    mapBaseSave(cLat, cLon, mpp, style, view, mapImg);   // guarda p/ abrir instantaneo na proxima vez
  }
  if(err[0])
  {
    static int fails = 0;
    fails = mapReady ? 0 : fails + 1;
    lock(); strlcpy(status, fails < 3 ? T("Loading map…","Carregando o mapa…","Cargando mapa…") : err, sizeof(status)); unlock();
    mapTryAt = millis() + (fails && fails < 3 ? 8000 : 60000);
  }
}

#ifdef R360_SIM
// So no simulador: um mapa pronto (sem internet)
void flightsSimMap(const uint16_t *img, uint8_t view)
{
  if(!mapImg) mapImg = (uint16_t *)heap_caps_malloc(LCD_W * LCD_H * 2, MALLOC_CAP_SPIRAM);
  memcpy(mapImg, img, LCD_W * LCD_H * 2);
  if(featMapNeedsDim(view)) mapTint(mapImg, 90, 0x00140Fu);
  else if(view == VIEW_BRIGHT) mapTint(mapImg, 200, 0);
  mapView = view; mapReady = true; mapGen = featGen; mapTryAt = millis() + 600000;
}
#endif

void flightsBegin()
{
  mtx = xSemaphoreCreateMutex();
}

void flightsWork()
{
  if(uiCurrent() != SCR_FLIGHTS || !netConnected()) return;
  bool viewChanged = false;
  if(feat.fStyle == VIEW_AUTO)
  {
    static uint32_t checkAt = 0;
    if((int32_t)(millis() - checkAt) >= 0)
    {
      checkAt = millis() + 60000;
      double cLat, cLon; featFlightsCenter(cLat, cLon, nullptr);
      viewChanged = featViewNow(VIEW_AUTO, cLat, cLon) != mapView;
    }
  }
  if((mapGen != featGen || viewChanged) && (int32_t)(millis() - mapTryAt) >= 0) buildMap();
  if(!lastFetch || opened || millis() - lastFetch >= feat.fRefresh * 1000UL)
  {
    opened = false;
    lastFetch = millis();
    fetchFlights();
  }
  if(routeWanted[0])
  {
    char c[10]; lock(); strlcpy(c, routeWanted, sizeof(c)); routeWanted[0] = 0; unlock();
    fetchRoute(c);
  }
}

void flightsOpen() { opened = true; if(!lastFetch) strlcpy(status, netConnected() ? T("Searching planes…","Buscando aviões…","Buscando aviones…") : T("No internet","Sem internet","Sin internet"), sizeof(status)); }
void flightsClose() { inDetail = false; }
bool flightsAnimated() { return !inDetail && feat.fSweep; }
bool flightsInDetail() { return inDetail; }
int flightsCount() { return planeCount; }
const char *flightsStatus() { return status; }

static int indexOfSel()
{
  for(int i = 0; i < planeCount; i++) if(!strcmp(planes[i].hex, selHex)) return i;
  return -1;
}

static void requestRoute(const Plane &p)
{
  if(!p.call[0]) return;
  for(const auto &r : routes) if(!strcmp(r.call, p.call) && millis() - r.at < 30 * 60000UL) return;
  strlcpy(routeWanted, p.call, sizeof(routeWanted));
}

void flightsLongPress()
{
  lock();
  if(!inDetail && planeCount)
  {
    inDetail = true;
    if(indexOfSel() < 0) strlcpy(selHex, planes[0].hex, sizeof(selHex));
    const int i = indexOfSel();
    if(i >= 0) requestRoute(planes[i]);
  }
  else inDetail = false;
  unlock();
}

bool flightsStep(int d)
{
  if(!inDetail) return false;
  lock();
  if(planeCount)
  {
    int i = indexOfSel(); if(i < 0) i = 0;
    i = ((i + d) % planeCount + planeCount) % planeCount;
    strlcpy(selHex, planes[i].hex, sizeof(selHex));
    requestRoute(planes[i]);
  }
  unlock();
  return true;
}

bool flightsShortPress()
{
  if(!inDetail) return false;
  lock();
  if(planeCount)
  {
    int i = indexOfSel();
    i = (i + 1) % planeCount;
    strlcpy(selHex, planes[i].hex, sizeof(selHex));
    requestRoute(planes[i]);
  }
  unlock();
  return true;
}

// ---- Desenho ----
static uint32_t altColor(int32_t ft)
{
  if(ft < 0) return 0x9AA5A3u;
  if(ft < 10000) return 0xFFD65Au;
  if(ft < 25000) return 0x5FFFD2u;
  return 0x7FD8FFu;
}

static uint32_t altColorBright(int32_t ft)
{
  if(ft < 0) return 0x555F5Du;
  if(ft < 10000) return 0xC26A00u;
  if(ft < 25000) return 0x00806Au;
  return 0x1F5FBFu;
}

static void planeShape(float x, float y, float trk, uint32_t col, float size)
{
  const float a = (trk - 90) * DEG_TO_RAD;
  const float ca = cosf(a), sa = sinf(a);
  auto P = [&](float fx, float fy, float &ox, float &oy) { ox = x + fx * ca - fy * sa; oy = y + fx * sa + fy * ca; };
  float x0, y0, x1, y1, x2, y2, x3, y3;
  P(size, 0, x0, y0); P(-size * 0.7f, size * 0.62f, x1, y1); P(-size * 0.35f, 0, x2, y2); P(-size * 0.7f, -size * 0.62f, x3, y3);
  canvas.fillTriangle(x0, y0, x1, y1, x2, y2, col);
  canvas.fillTriangle(x0, y0, x2, y2, x3, y3, col);
  gfxBlendLine(x0, y0, x1, y1, col, 200); gfxBlendLine(x1, y1, x2, y2, col, 140);
  gfxBlendLine(x2, y2, x3, y3, col, 140); gfxBlendLine(x3, y3, x0, y0, col, 200);
}

static void drawRadar()
{
  const bool haveMap = feat.fStyle != VIEW_PLAIN && mapReady && mapImg;
  const bool bright = haveMap && mapView == VIEW_BRIGHT;
  if(haveMap) memcpy(canvas.getBuffer(), mapImg, LCD_W * LCD_H * 2);
  else canvas.fillScreen(C_BG);

  const float kmPerPx = feat.fRadiusKm / (float)R_PX;
  const uint32_t ringCol = bright ? 0x2E6E62u : haveMap ? 0x3FA08Cu : C_RING2;
  const uint32_t labelCol = bright ? 0x0B1716u : C_TEXT;
  if(feat.fRings)
  {
    for(int k = 1; k <= 3; k++)
    {
      const int r = R_PX * k / 3;
      canvas.drawCircle(CX, CY, r, ringCol);
      char b[16]; fmtDist(b, sizeof(b), feat.fRadiusKm * k / 3.0f);
      gfxText(b, CX + 4, CY - r + 10, FontSmall, bright ? 0x1F5A50u : haveMap ? 0x9FE8D8u : C_MUTED, middle_left);
    }
    canvas.drawFastHLine(CX - R_PX, CY, 2 * R_PX, ringCol);
    canvas.drawFastVLine(CX, CY - R_PX, 2 * R_PX, ringCol);
  }

  // Varredura com rastro que se apaga
  const float sweep = fmodf(millis() / 4000.0f * 360.0f, 360.0f);
  if(feat.fSweep)
  {
    for(int k = 0; k < 36; k++)
    {
      const float a = (sweep - k * 1.2f - 90) * DEG_TO_RAD;
      const uint8_t alpha = (uint8_t)(170 * (1 - k / 36.0f) * (1 - k / 36.0f));
      gfxBlendLine(CX, CY, CX + cosf(a) * R_PX, CY + sinf(a) * R_PX, bright ? 0x0E8C6Eu : C_ACCENT, k == 0 ? 255 : alpha);
    }
  }

  // Tamanho dos avioes e dos textos (Recursos -> Voos): o icone escala livre, o texto troca de fonte
  static const float SIZE_K[] = {0.75f, 1.0f, 1.3f, 1.6f};
  const int sz = feat.fSize <= 3 ? feat.fSize : 1;
  const float k = SIZE_K[sz];
  const uint8_t *lf = sz == 0 ? FxInter13 : sz == 1 ? FontSmall : FontBody;
  gfxFont(lf);
  const int lineH = canvas.fontHeight() + 1;
  const int silPx = (int)lroundf(30 * k);
  lock();
  struct Box { int x0, y0, x1, y1; } boxes[MAX_PLANES]; int nb = 0;
  for(int i = planeCount - 1; i >= 0; i--)   // mais perto por cima
  {
    const Plane &p = planes[i];
    const float r = p.distKm / kmPerPx;
    const float a = (p.brg - 90) * DEG_TO_RAD;
    const uint32_t colA = bright ? altColorBright(p.alt) : altColor(p.alt);
    if(r > R_PX)
    {
      // fora do raio: ponto na borda
      canvas.fillSmoothCircle(CX + cosf(a) * (R_PX + 8), CY + sinf(a) * (R_PX + 8), 3 * k, feat.fColorBy == 2 ? feat.fColor : colA);
      continue;
    }
    const float x = CX + cosf(a) * r, y = CY + sinf(a) * r;
    const bool sel = !strcmp(p.hex, selHex);
    const uint32_t col = feat.fColorBy == 2 ? feat.fColor
                       : feat.fColorBy == 1 && p.cat >= 0 && p.alt >= 0 ? (bright ? CAT_COL_BRIGHT[p.cat] : CAT_COL[p.cat]) : colA;
    if(feat.fVectors && p.gs > 30)
    {
      const float len = min(60.0f, p.gs * 1.852f / 60.0f / kmPerPx * 2);   // 2 minutos a frente
      const float ta = (p.track - 90) * DEG_TO_RAD;
      gfxBlendLine(x, y, x + cosf(ta) * len, y + sinf(ta) * len, col, 150);
    }
    const uint8_t *sil = feat.fIcon == 0 ? packSil30(p.cat, (int)lroundf(p.track / 10.0f)) : nullptr;
    if(sil)
    {
      if(sel && !bright) gfxGlow(x, y, 22 * k, col, 90);
      if(sz == 1)
      {
        if(!bright) gfxMask(sil, 30, 30, x - 15 + 1, y - 15 + 2, 0x000000u, 150);   // sombra
        gfxMask(sil, 30, 30, x - 15, y - 15, col);
      }
      else
      {
        if(!bright) gfxMaskScaled(sil, 30, 30, x + 1, y + 2, silPx, 0x000000u, 150);
        gfxMaskScaled(sil, 30, 30, x, y, silPx, col);
      }
      if(sel) canvas.drawCircle(x, y, 18 * k, labelCol);
    }
    else
    {
      planeShape(x, y, p.track, col, 8 * k);
      if(sel) canvas.drawCircle(x, y, 13 * k, labelCol);
    }

    // Rotulos sem sobreposicao
    char l1[16] = "", l2[16] = "";
    if(feat.fCallsign) strlcpy(l1, p.call[0] ? p.call : p.hex, sizeof(l1));
    if(feat.fAltitude) fmtAlt(l2, sizeof(l2), p.alt);
    else if(feat.fType) strlcpy(l2, p.type, sizeof(l2));
    if(!l1[0] && !l2[0]) continue;
    gfxFont(lf);
    const int w = max(canvas.textWidth(l1), canvas.textWidth(l2));
    const int h = (l1[0] && l2[0]) ? 2 * lineH : lineH;
    const int off = (int)lroundf((sil ? 16 : 11) * k);
    int lx = x + off, ly = y - h / 2;
    if(lx + w > CX + gfxRoundHalfWidth(ly + h / 2, 6)) lx = x - off - w;
    bool clash = false;
    for(int k = 0; k < nb; k++) if(lx < boxes[k].x1 && lx + w > boxes[k].x0 && ly < boxes[k].y1 && ly + h > boxes[k].y0) { clash = true; break; }
    if(clash) continue;
    boxes[nb++] = {lx, ly, lx + w, ly + h};
    int ty = ly + lineH / 2;
    if(l1[0]) { gfxText(l1, lx, ty, lf, labelCol, middle_left); ty += lineH; }
    if(l2[0]) gfxText(l2, lx, ty, lf, col, middle_left);
  }
  const int count = planeCount;
  char st[64]; strlcpy(st, status, sizeof(st));
  unlock();

  // Centro (sua posicao ou o aeroporto)
  canvas.fillSmoothCircle(CX, CY, 4, labelCol);
  canvas.drawCircle(CX, CY, 7, labelCol);
  const char *centerLabel = nullptr; double dl, dn; featFlightsCenter(dl, dn, &centerLabel);
  if(centerLabel)
  {
    const int w = gfxTextWidth(centerLabel, FontSmall) + 18;
    canvas.fillSmoothRoundRect(CX - w / 2, 30, w, 24, 12, bright ? 0xFFFFFFu : 0x0B1716u);
    gfxText(centerLabel, CX, 42, FontSmall, bright ? 0x0B1716u : C_ACCENT);
  }

  char b[40];
  if(st[0]) gfxTextFit(st, CX, 318, 200, FontSmall, C_WARN);
  else { snprintf(b, sizeof(b), count == 1 ? T("%d plane","%d avião","%d avión") : T("%d planes","%d aviões","%d aviones"), count); gfxText(b, CX, 318, FontSmall, bright ? 0x1F3A36u : C_MUTED); }
  if(haveMap) gfxText(mapAttribution(featMapStyle(mapView)), CX, 340, FontSmall, bright ? 0x3D5C56u : 0x6C8C86u);
}

static void drawDetail()
{
  if(!gfxImage("bg_obsidian")) gfxBackground();
  lock();
  int i = indexOfSel();
  if(i < 0 && planeCount) { i = 0; strlcpy(selHex, planes[0].hex, sizeof(selHex)); }
  if(i < 0) { unlock(); inDetail = false; gfxStatusLine(T("No plane","Nenhum avião","Sin avión"), C_MUTED); return; }
  const Plane p = planes[i];
  const int count = planeCount;
  Route r; memset(&r, 0, sizeof(r));
  for(const auto &x : routes) if(!strcmp(x.call, p.call)) r = x;
  unlock();

  char b[64];
  const int cat = p.cat >= 0 && p.cat <= 6 ? p.cat : CAT_NARROW;
  const uint8_t *big = packSil150(cat);
  if(big)
  {
    gfxGlow(CX, 98, 92, CAT_COL[cat], 60);
    gfxMaskScaled(big, 150, 150, CX + 3, 101, 112, 0x000000u, 140);
    gfxMaskScaled(big, 150, 150, CX, 98, 112, 0xE6F7F1u);
  }
  else gfxText(p.call[0] ? p.call : p.hex, CX, 98, FontTitle, C_ACCENT);

  gfxTextShadowFit(p.type[0] ? typeName(p.type) : T("Unknown model","Modelo desconhecido","Modelo desconocido"), CX, 172, 270, FontTitle, C_TEXT);
  snprintf(b, sizeof(b), "%s · %s%s%s", p.call[0] ? p.call : p.hex, CAT_NAMES[uiLang][cat], r.airline[0] ? " · " : "", r.airline);
  gfxTextFit(b, CX, 199, 280, FontSmall, CAT_COL[cat]);

  if(r.ok)
  {
    snprintf(b, sizeof(b), "%s  →  %s", r.from, r.to);
    gfxText(b, CX, 228, FontBody, C_TEXT);
  }
  else gfxText(r.call[0] ? T("Unknown route","Rota desconhecida","Ruta desconocida") : T("Finding route…","Buscando rota…","Buscando ruta…"), CX, 228, FontSmall, C_MUTED);

  struct Cell { const char *label; char value[24]; uint32_t col; } cells[5];
  cells[0].label = "Altitude"; fmtAlt(cells[0].value, sizeof(cells[0].value), p.alt); cells[0].col = altColor(p.alt);
  cells[1].label = "Velocidade"; fmtSpeed(cells[1].value, sizeof(cells[1].value), p.gs); cells[1].col = C_TEXT;
  cells[2].label = "Distância"; fmtDist(cells[2].value, sizeof(cells[2].value), p.distKm); cells[2].col = C_TEXT;
  cells[3].label = "Rumo"; snprintf(cells[3].value, sizeof(cells[3].value), "%d° %s", (int)lroundf(p.track), compass(p.track)); cells[3].col = C_TEXT;
  cells[4].label = p.reg[0] ? "Matrícula" : "Vertical";
  if(p.reg[0]) strlcpy(cells[4].value, p.reg, sizeof(cells[4].value));
  else if(abs(p.vrate) < 200) strlcpy(cells[4].value, T("level","nivelado","nivelado"), sizeof(cells[4].value));
  else snprintf(cells[4].value, sizeof(cells[4].value), "%s %d fpm", p.vrate > 0 ? "↑" : "↓", abs(p.vrate));
  cells[4].col = C_TEXT;
  for(int k = 0; k < 3; k++)
  {
    const int x = CX + (k - 1) * 100;
    gfxText(cells[k].label, x, 258, FontSmall, C_MUTED);
    if(gfxTextWidth(cells[k].value, FontBody) <= 98) gfxText(cells[k].value, x, 280, FontBody, cells[k].col);
    else gfxTextFit(cells[k].value, x, 280, 98, FontSmall, cells[k].col);
  }
  for(int k = 3; k < 5; k++)
  {
    const int x = CX + (k == 3 ? -54 : 54);
    gfxText(cells[k].label, x, 306, FontSmall, C_MUTED);
    gfxTextFit(cells[k].value, x, 326, 100, FontBody, cells[k].col);
  }
  snprintf(b, sizeof(b), "%d de %d", i + 1, count);
  gfxText(b, CX, 348, FontSmall, C_MUTED);
}

void flightsDraw()
{
  if(inDetail) drawDetail(); else drawRadar();
}
