#include "Feat.h"
#include <LittleFS.h>
#include <math.h>
#include <time.h>
#include "Airports.h"
#include "Config.h"

FeatCfg feat;
static SemaphoreHandle_t stateMtx = nullptr;
void stateLock() { if(!stateMtx) stateMtx = xSemaphoreCreateRecursiveMutex(); xSemaphoreTakeRecursive(stateMtx, portMAX_DELAY); }
void stateUnlock() { if(stateMtx) xSemaphoreGiveRecursive(stateMtx); }
volatile uint32_t featGen = 1;
static const char *PATH = "/features.json";

static void defaults()
{
  memset(&feat, 0, sizeof(feat));
  feat.lat = 51.5074; feat.lon = -0.1278;
  strlcpy(feat.place, "London", sizeof(feat.place));
  feat.mapSrc = 0;
  feat.scrFlights = true; feat.scrWxRadar = true; feat.scrWxNow = true; feat.scrWxHours = true; feat.scrWxDays = true;
  feat.cStyle = CLK_REAL;
  feat.scrPhotos = false; feat.scrNews = false; feat.scrDmr = false; feat.scrHoro = false; feat.scrMoon = false; feat.scrPc = false; feat.pcHost[0] = 0; feat.pcStyle = 0; feat.pcSecs = 2;
  for(int i = 0; i < 24; i++) feat.order[i] = i; feat.scrNetwork = true; feat.scrWifi = false;
  feat.scrQuotes = false; strlcpy(feat.qLocal, "BRL", sizeof(feat.qLocal));
  strlcpy(feat.qItems[0], "USD", 8); strlcpy(feat.qItems[1], "EUR", 8); strlcpy(feat.qItems[2], "BTC", 8); feat.qCount = 3;
  feat.qStyle = 0; feat.qSecs = 6; feat.qRefresh = 5;

  feat.fSource = 2; feat.fRadiusKm = 50; feat.fRefresh = 10;
  feat.fMinAlt = 0; feat.fMaxAlt = 60000; feat.fMax = 24;
  feat.fStyle = VIEW_DARK; feat.fSweep = true; feat.fVectors = true; feat.fCallsign = true; feat.fAltitude = true;
  feat.fType = false; feat.fGround = false; feat.fRings = true;
  feat.fDistU = 1; feat.fAltU = 0; feat.fSpdU = 1; feat.fCenter = 0;
  feat.fIcon = 0; feat.fColorBy = 0; feat.fColor = 0xFFC857; feat.fSize = 1; feat.fTypes = 0x7F;

  feat.wZoom = 7; feat.wFrames = 8; feat.wStyle = VIEW_DARK; feat.wFrameMs = 550; feat.wCards = true; feat.wCardStyle = 1; feat.wHoursStyle = 0; feat.wDaysStyle = 0; feat.wNowStyle = 0;
  feat.wOwn = false; feat.wLat = feat.lat; feat.wLon = feat.lon; strlcpy(feat.wPlace, feat.place, sizeof(feat.wPlace));

  feat.pSecs = 15; feat.pShuffle = false; feat.pFade = true; feat.pStyle = PCLK_FOOTER; feat.pWxPop = true;

  feat.nTopics = 0x0003; feat.nSecs = 12; feat.nRefreshMin = 30; feat.nStyle = 0; feat.nUrgent = true;

  strlcpy(feat.dHost, "pi-star.local", sizeof(feat.dHost));
  feat.dPollSecs = 4; feat.dLookup = true; feat.dStyle = 0;
  feat.hSigns = 1 << 4; feat.hTheme = 0; feat.hSecs = 15; feat.hScores = true;
}

void featToJson(JsonObject o)
{
  o["lat"] = feat.lat; o["lon"] = feat.lon; o["place"] = feat.place; o["mapSrc"] = feat.mapSrc;
  JsonObject s = o["screens"].to<JsonObject>();
  s["flights"] = feat.scrFlights; s["wxRadar"] = feat.scrWxRadar; s["wxNow"] = feat.scrWxNow; s["wxHours"] = feat.scrWxHours; s["wxDays"] = feat.scrWxDays;
  s["photos"] = feat.scrPhotos; s["news"] = feat.scrNews; s["dmr"] = feat.scrDmr; s["horo"] = feat.scrHoro; s["moon"] = feat.scrMoon; s["pc"] = feat.scrPc; s["network"] = feat.scrNetwork; s["quotes"] = feat.scrQuotes; s["wifi"] = feat.scrWifi;

  o["clock"].to<JsonObject>()["style"] = feat.cStyle;

  JsonObject f = o["flights"].to<JsonObject>();
  f["source"] = feat.fSource; f["radiusKm"] = feat.fRadiusKm; f["refresh"] = feat.fRefresh;
  f["minAlt"] = feat.fMinAlt; f["maxAlt"] = feat.fMaxAlt; f["max"] = feat.fMax; f["style"] = feat.fStyle;
  f["sweep"] = feat.fSweep; f["vectors"] = feat.fVectors; f["callsign"] = feat.fCallsign; f["altitude"] = feat.fAltitude;
  f["type"] = feat.fType; f["ground"] = feat.fGround; f["rings"] = feat.fRings;
  f["distU"] = feat.fDistU; f["altU"] = feat.fAltU; f["spdU"] = feat.fSpdU; f["center"] = feat.fCenter; f["icao"] = feat.fIcao;
  f["icon"] = feat.fIcon; f["colorBy"] = feat.fColorBy; f["types"] = feat.fTypes; f["size"] = feat.fSize;
  { char c[8]; snprintf(c, sizeof(c), "#%06lX", (unsigned long)(feat.fColor & 0xFFFFFF)); f["color"] = c; }

  JsonObject w = o["weather"].to<JsonObject>();
  w["zoom"] = feat.wZoom; w["frames"] = feat.wFrames; w["style"] = feat.wStyle; w["own"] = feat.wOwn; w["lat"] = feat.wLat; w["lon"] = feat.wLon; w["place"] = feat.wPlace; w["frameMs"] = feat.wFrameMs; w["cards"] = feat.wCards; w["cardStyle"] = feat.wCardStyle; w["hoursStyle"] = feat.wHoursStyle; w["daysStyle"] = feat.wDaysStyle; w["nowStyle"] = feat.wNowStyle;

  JsonObject p = o["photos"].to<JsonObject>();
  p["secs"] = feat.pSecs; p["shuffle"] = feat.pShuffle; p["fade"] = feat.pFade; p["style"] = feat.pStyle; p["wxPop"] = feat.pWxPop;

  JsonObject n = o["news"].to<JsonObject>();
  n["topics"] = feat.nTopics; n["query"] = feat.nQuery; n["url"] = feat.nUrl; n["secs"] = feat.nSecs; n["refreshMin"] = feat.nRefreshMin; n["style"] = feat.nStyle; n["urgent"] = feat.nUrgent;

  JsonObject d = o["dmr"].to<JsonObject>();
  { JsonArray a = o["order"].to<JsonArray>(); for(int i = 0; i < FEAT_SCREENS; i++) a.add(feat.order[i]); }
  {
    JsonObject qo = o["quotes"].to<JsonObject>();
    qo["local"] = feat.qLocal; qo["style"] = feat.qStyle; qo["secs"] = feat.qSecs; qo["refresh"] = feat.qRefresh;
    JsonArray it = qo["items"].to<JsonArray>(); for(int i = 0; i < feat.qCount; i++) it.add(feat.qItems[i]);
  }
  { JsonObject pc = o["pc"].to<JsonObject>(); pc["host"] = feat.pcHost; pc["style"] = feat.pcStyle; pc["secs"] = feat.pcSecs; }
  d["host"] = feat.dHost; d["pollSecs"] = feat.dPollSecs; d["lookup"] = feat.dLookup; d["style"] = feat.dStyle;

  JsonObject h = o["horo"].to<JsonObject>();
  h["signs"] = feat.hSigns; h["theme"] = feat.hTheme; h["secs"] = feat.hSecs; h["scores"] = feat.hScores;
}

template<typename T> static void num(JsonObjectConst o, const char *k, T &v, long lo, long hi)
{
  if(o[k].is<long>() || o[k].is<double>()) { long x = o[k].as<long>(); v = (T)constrain(x, lo, hi); }
}
static void flag(JsonObjectConst o, const char *k, bool &v) { if(o[k].is<bool>()) v = o[k].as<bool>(); }
static void str(JsonObjectConst o, const char *k, char *dst, size_t n) { if(o[k].is<const char *>()) strlcpy(dst, o[k].as<const char *>(), n); }

void featOrderFix(int count)
{
  bool seen[24] = {};
  uint8_t out[24]; int n = 0;
  for(int i = 0; i < count && i < 24; i++)
  {
    const uint8_t id = feat.order[i];
    if(id < count && !seen[id]) { seen[id] = true; out[n++] = id; }
  }
  for(int id = 0; id < count && n < 24; id++) if(!seen[id]) out[n++] = id;   // telas novas entram no fim
  memcpy(feat.order, out, n);
}

static void featDropRemoved();

void featFromJson(JsonObjectConst o)
{
  struct Drop { ~Drop() { featDropRemoved(); } } drop;   // vale tambem para o que chega pelo painel
  if(o["lat"].is<double>()) feat.lat = constrain(o["lat"].as<double>(), -85.0, 85.0);
  if(o["lon"].is<double>()) feat.lon = constrain(o["lon"].as<double>(), -180.0, 180.0);
  str(o, "place", feat.place, sizeof(feat.place));
  num(o, "mapSrc", feat.mapSrc, 0, 1);
  JsonObjectConst s = o["screens"];
  if(!s.isNull())
  {
    flag(s, "flights", feat.scrFlights); flag(s, "wxRadar", feat.scrWxRadar); flag(s, "wxNow", feat.scrWxNow); flag(s, "wxHours", feat.scrWxHours); flag(s, "wxDays", feat.scrWxDays);
    flag(s, "photos", feat.scrPhotos); flag(s, "news", feat.scrNews); flag(s, "dmr", feat.scrDmr); flag(s, "horo", feat.scrHoro); flag(s, "moon", feat.scrMoon); flag(s, "pc", feat.scrPc); flag(s, "network", feat.scrNetwork); flag(s, "quotes", feat.scrQuotes); flag(s, "wifi", feat.scrWifi);
  }
  JsonObjectConst ck = o["clock"];
  if(!ck.isNull()) num(ck, "style", feat.cStyle, 0, CLK_COUNT - 1);
  JsonObjectConst f = o["flights"];
  if(!f.isNull())
  {
    num(f, "source", feat.fSource, 0, 2); num(f, "radiusKm", feat.fRadiusKm, 5, 250); num(f, "refresh", feat.fRefresh, 5, 120);
    num(f, "minAlt", feat.fMinAlt, 0, 60000); num(f, "maxAlt", feat.fMaxAlt, 500, 60000); num(f, "max", feat.fMax, 1, 40);
    num(f, "style", feat.fStyle, 0, 3);
    num(f, "distU", feat.fDistU, 0, 2); num(f, "altU", feat.fAltU, 0, 1); num(f, "spdU", feat.fSpdU, 0, 2); num(f, "center", feat.fCenter, 0, 1);
    if(f["icao"].is<const char *>()) { strlcpy(feat.fIcao, f["icao"].as<const char *>(), sizeof(feat.fIcao)); for(char *c = feat.fIcao; *c; c++) *c = toupper(*c); }
    flag(f, "sweep", feat.fSweep); flag(f, "vectors", feat.fVectors); flag(f, "callsign", feat.fCallsign); flag(f, "altitude", feat.fAltitude);
    flag(f, "type", feat.fType); flag(f, "ground", feat.fGround); flag(f, "rings", feat.fRings);
    num(f, "icon", feat.fIcon, 0, 1); num(f, "colorBy", feat.fColorBy, 0, 2); num(f, "types", feat.fTypes, 0, 0x7F);
    num(f, "size", feat.fSize, 0, 3);
    if(f["color"].is<const char *>()) { const char *c = f["color"]; if(c[0] == '#' && strlen(c) == 7) feat.fColor = strtoul(c + 1, nullptr, 16) & 0xFFFFFF; }
  }
  JsonObjectConst w = o["weather"];
  if(!w.isNull())
  {
    num(w, "zoom", feat.wZoom, 3, 7); num(w, "frames", feat.wFrames, 2, 10); num(w, "style", feat.wStyle, 0, 3);
    flag(w, "own", feat.wOwn); str(w, "place", feat.wPlace, sizeof(feat.wPlace));
    if(w["lat"].is<double>()) feat.wLat = constrain(w["lat"].as<double>(), -85.0, 85.0);
    if(w["lon"].is<double>()) feat.wLon = constrain(w["lon"].as<double>(), -180.0, 180.0);
    num(w, "frameMs", feat.wFrameMs, 200, 3000); flag(w, "cards", feat.wCards); num(w, "cardStyle", feat.wCardStyle, 0, 10); num(w, "hoursStyle", feat.wHoursStyle, 0, 2); num(w, "daysStyle", feat.wDaysStyle, 0, 1); num(w, "nowStyle", feat.wNowStyle, 0, 1);
  }
  JsonObjectConst p = o["photos"];
  if(!p.isNull())
  {
    num(p, "secs", feat.pSecs, 3, 3600); flag(p, "shuffle", feat.pShuffle); flag(p, "fade", feat.pFade);
    if(p["clock"].is<bool>() && !p["style"].is<int>()) feat.pStyle = p["clock"].as<bool>() ? PCLK_FOOTER : PCLK_NONE;   // versao antiga
    num(p, "style", feat.pStyle, 0, PCLK_COUNT - 1); flag(p, "wxPop", feat.pWxPop);
  }
  JsonObjectConst n = o["news"];
  if(!n.isNull())
  {
    num(n, "topics", feat.nTopics, 0, 0x1FF); str(n, "query", feat.nQuery, sizeof(feat.nQuery)); str(n, "url", feat.nUrl, sizeof(feat.nUrl));
    num(n, "secs", feat.nSecs, 4, 120); num(n, "refreshMin", feat.nRefreshMin, 5, 240); num(n, "style", feat.nStyle, 0, 5); flag(n, "urgent", feat.nUrgent);
  }
  JsonObjectConst d = o["dmr"];
  if(o["order"].is<JsonArrayConst>())
  {
    uint8_t tmp[24]; int n = 0;
    for(JsonVariantConst v : o["order"].as<JsonArrayConst>()) if(n < 24 && v.is<int>()) { const int id = v.as<int>(); tmp[n++] = (id >= 0 && id < 255) ? id : 255; }
    for(int i = 0; i < 24; i++) feat.order[i] = i < n ? tmp[i] : 255;
    featOrderFix(FEAT_SCREENS);
  }
  {
    JsonObjectConst qo = o["quotes"];
    if(!qo.isNull())
    {
      // codigos so com letras/numeros, em maiusculas (vai na URL)
      auto clean = [](const char *in, char *out, size_t n) { size_t j = 0; for(const char *p = in; *p && j + 1 < n; p++) if(isalnum((unsigned char)*p)) out[j++] = toupper((unsigned char)*p); out[j] = 0; };
      if(qo["local"].is<const char *>()) { char c[6]; clean(qo["local"].as<const char *>(), c, sizeof(c)); if(c[0]) strlcpy(feat.qLocal, c, sizeof(feat.qLocal)); }
      num(qo, "style", feat.qStyle, 0, 1); num(qo, "secs", feat.qSecs, 2, 60); num(qo, "refresh", feat.qRefresh, 1, 120);
      if(qo["items"].is<JsonArrayConst>())
      {
        feat.qCount = 0;
        for(JsonVariantConst v : qo["items"].as<JsonArrayConst>())
        {
          if(feat.qCount >= QUOTES_MAX || !v.is<const char *>()) continue;
          char c[8]; clean(v.as<const char *>(), c, sizeof(c));
          bool dup = !c[0];
          for(int k = 0; k < feat.qCount; k++) if(!strcmp(feat.qItems[k], c)) dup = true;
          if(!dup) strlcpy(feat.qItems[feat.qCount++], c, 8);
        }
      }
    }
  }
  { JsonObjectConst pc = o["pc"]; if(!pc.isNull()) { str(pc, "host", feat.pcHost, sizeof(feat.pcHost)); num(pc, "style", feat.pcStyle, 0, 2); num(pc, "secs", feat.pcSecs, 1, 60); } }
  if(!d.isNull()) { str(d, "host", feat.dHost, sizeof(feat.dHost)); num(d, "pollSecs", feat.dPollSecs, 2, 60); flag(d, "lookup", feat.dLookup); num(d, "style", feat.dStyle, 0, 5); }
  JsonObjectConst hh = o["horo"];
  if(!hh.isNull()) { num(hh, "signs", feat.hSigns, 0, 0x0FFF); num(hh, "theme", feat.hTheme, 0, 2); num(hh, "secs", feat.hSecs, 0, 120); flag(hh, "scores", feat.hScores); }
  if(feat.fMaxAlt < feat.fMinAlt) feat.fMaxAlt = feat.fMinAlt + 1000;
  featGen = featGen + 1;
}

// Temas removidos na limpeza: quem usava um deles passa para o mais parecido que ficou
static void featDropRemoved()
{
  if(feat.cStyle == CLK_DECO_REMOVED || feat.cStyle == CLK_RADIO_REMOVED || feat.cStyle == CLK_AVCLOCK_REMOVED) feat.cStyle = CLK_REAL;
  if(feat.pStyle == PCLK_ARC_REMOVED || feat.pStyle == PCLK_GLASS_REMOVED) feat.pStyle = PCLK_FOOTER;
  if(feat.wCardStyle == 0 || feat.wCardStyle == 3 || feat.wCardStyle == 8) feat.wCardStyle = 1;
  feat.wNowStyle = 0;   // "Simples" saiu do menu (continua so para o modo seguro)
}

void featLoad()
{
  defaults();
  File f = LittleFS.open(PATH, "r");
  if(!f) return;
  JsonDocument doc;
  if(!deserializeJson(doc, f)) featFromJson(doc.as<JsonObjectConst>());
  f.close();
  featGen = featGen + 1;
}

bool featSave()
{
  JsonDocument doc;
  featToJson(doc.to<JsonObject>());
  File f = LittleFS.open("/features.tmp", "w");
  if(!f) return false;
  serializeJson(doc, f);
  f.close();
  LittleFS.remove(PATH);
  const bool ok = LittleFS.rename("/features.tmp", PATH);
  return ok;
}

bool featIsDaylight(double lat, double lon)
{
  const time_t now = time(nullptr);
  if(now < 1700000000) return true;
  const double R = M_PI / 180;
  const double d = now / 86400.0 + 2440587.5 - 2451545.0;
  const double g = fmod(357.529 + 0.98560028 * d, 360.0) * R;
  const double q = fmod(280.459 + 0.98564736 * d, 360.0);
  const double L = (q + 1.915 * sin(g) + 0.020 * sin(2 * g)) * R;
  const double e = (23.439 - 0.00000036 * d) * R;
  const double ra = atan2(cos(e) * sin(L), cos(L));
  const double dec = asin(sin(e) * sin(L));
  const double gmst = fmod(280.46061837 + 360.98564736629 * d, 360.0) * R;
  const double H = gmst + lon * R - ra, phi = lat * R;
  const double alt = asin(sin(phi) * sin(dec) + cos(phi) * cos(dec) * cos(H)) / R;
  return alt > -4.0;
}

uint8_t featViewNow(uint8_t view, double lat, double lon)
{
  if(view != VIEW_AUTO) return view;
  return featIsDaylight(lat, lon) ? VIEW_BRIGHT : VIEW_DARK;
}

uint8_t featMapStyle(uint8_t view)
{
  if(view == VIEW_PLAIN) return MAP_NONE;
  if(feat.mapSrc == 1 || !cfg.cartoKey[0]) return MAP_OSM;   // CARTO sem chave sai com aviso por cima: usa OSM
  return view == VIEW_BRIGHT ? MAP_LIGHT : MAP_DARK;
}

bool featMapNeedsDim(uint8_t view) { return view == VIEW_DARK && featMapStyle(view) == MAP_OSM; }

void featFlightsCenter(double &lat, double &lon, const char **label)
{
  if(feat.fCenter == 1 && feat.fIcao[0])
  {
    const AirportRec *a = airportFind(feat.fIcao);
    if(a) { lat = a->lat; lon = a->lon; if(label) *label = feat.fIcao; return; }
  }
  lat = feat.lat; lon = feat.lon; if(label) *label = nullptr;
}

void featWeatherCenter(double &lat, double &lon, const char **label)
{
  if(feat.wOwn) { lat = feat.wLat; lon = feat.wLon; if(label) *label = feat.wPlace; return; }
  lat = feat.lat; lon = feat.lon; if(label) *label = feat.place;
}
