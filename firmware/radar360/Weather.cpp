#include "Weather.h"
#include "Lang.h"
#include "Gfx.h"
#include "Feat.h"
#include "Http.h"
#include "Json.h"
#include "MapImg.h"
#include "Net.h"
#include "Ui.h"
#include "Crash.h"
#include "Sky.h"
#include "RadarCard.h"
#include <math.h>
#include <time.h>

static SemaphoreHandle_t mtx;
static void lock() { xSemaphoreTake(mtx, portMAX_DELAY); }
static void unlock() { xSemaphoreGive(mtx); }

// ---- Dados do Open-Meteo ----
#define MAX_DAYS 7
#define MAX_HOURS 24
static WxNow now_;
static WxDay days[MAX_DAYS];
static int dayCount = 0;
static WxHour hours[MAX_HOURS];
static int hourCount = 0;
static float quarter[8];          // chuva (mm) a cada 15 min nas proximas 2 h
static time_t quarter0 = 0;
static int quarterCount = 0;
static bool haveWx = false;
static time_t wxTime = 0;
static uint32_t wxFetchAt = 0;
static uint32_t wxGen = 0;
static char wxStatus[64] = "Buscando o tempo…";

// ---- Radar de chuva (RainViewer) ----
#define MAX_FRAMES 10
static uint16_t *frames[MAX_FRAMES];
static time_t frameTime[MAX_FRAMES];
static volatile int frameCount = 0;
static uint32_t radarFetchAt = 0;
static uint32_t radarGen = 0;
static char radarStatus[64] = "Preparing the radar…";
static int radarFails = 0;   // falhas seguidas: so mostra o erro depois de algumas
static bool cardsShown = true;
static uint32_t radarTimeStampSeen = 0;
static volatile uint8_t radarView = VIEW_DARK;   // estilo (resolvido) do mapa dos quadros atuais

const char *weatherCondition(int c)
{
  switch(c)
  {
    case 0: return T("Clear sky","Céu limpo","Cielo despejado");
    case 1: return T("Few clouds","Poucas nuvens","Pocas nubes");
    case 2: return T("Partly cloudy","Parcialmente nublado","Parcialmente nublado");
    case 3: return T("Cloudy","Nublado","Nublado");
    case 45: case 48: return T("Fog","Neblina","Niebla");
    case 51: case 53: case 55: return T("Drizzle","Garoa","Llovizna");
    case 56: case 57: return T("Freezing drizzle","Garoa congelante","Llovizna helada");
    case 61: return T("Light rain","Chuva fraca","Lluvia ligera");
    case 63: return T("Rain","Chuva","Lluvia");
    case 65: return T("Heavy rain","Chuva forte","Lluvia fuerte");
    case 66: case 67: return T("Freezing rain","Chuva congelante","Lluvia helada");
    case 71: case 73: case 75: case 77: return T("Snow","Neve","Nieve");
    case 80: return T("Light showers","Pancadas fracas","Chubascos ligeros");
    case 81: return T("Rain showers","Pancadas de chuva","Chubascos");
    case 82: return T("Heavy showers","Pancadas fortes","Chubascos fuertes");
    case 85: case 86: return T("Snow showers","Pancadas de neve","Chubascos de nieve");
    case 95: return T("Thunderstorm","Trovoadas","Tormenta");
    case 96: case 99: return T("Thunderstorm with hail","Trovoadas com granizo","Tormenta con granizo");
    default: return "—";
  }
}

const char *weatherConditionShort(int c)
{
  switch(c)
  {
    case 2: return T("Partly cloudy","Parc. nublado","Parc. nublado");
    case 56: case 57: return T("Icy drizzle","Garoa gelada","Llovizna helada");
    case 66: case 67: return T("Icy rain","Chuva gelada","Lluvia helada");
    case 96: case 99: return T("Hail","Granizo","Granizo");
    case 82: return T("Heavy showers","Pancadas fortes","Chubascos fuertes");
    default: return weatherCondition(c);
  }
}

bool weatherSummary(float *t, int *code, bool *day)
{
  if(!haveWx) return false;
  lock(); *t = now_.temp; *code = now_.code; *day = now_.day; unlock();
  return true;
}

bool weatherNow(WxNow *o)
{
  if(!haveWx) return false;
  lock(); *o = now_; unlock();
  return true;
}

int weatherHours(WxHour *out, int max)
{
  if(!haveWx) return 0;
  lock();
  // pula as horas que ja passaram desde a ultima busca
  const time_t now = time(nullptr);
  int first = 0;
  while(first < hourCount - 1 && hours[first + 1].t <= now) first++;
  const int n = min(max, hourCount - first);
  for(int i = 0; i < n; i++) out[i] = hours[first + i];
  unlock();
  return n;
}

int weatherDays(WxDay *out, int max)
{
  if(!haveWx) return 0;
  lock(); const int n = min(max, dayCount); memcpy(out, days, sizeof(WxDay) * n); unlock();
  return n;
}

const char *weatherPlace()
{
  const char *pl = nullptr; double a, b; featWeatherCenter(a, b, &pl);
  if(!pl) return "";
  // strip common place-name prefixes so the name fits the round screen
  static const char *const PREFIX[] = {"Aeroporto Internacional de ", "Aeroporto Estadual de ", "Aeroporto de ", "Aeroporto ", "Município de ", "Cidade de "};
  for(const char *p : PREFIX) { const size_t n = strlen(p); if(!strncasecmp(pl, p, n) && pl[n]) return pl + n; }
  return pl;
}

static bool rainy(int c) { return (c >= 51 && c <= 67) || (c >= 80 && c <= 82) || c >= 95; }

bool weatherAlert(char *title, size_t tl, char *sub, size_t sl)
{
  if(!haveWx) return false;
  WxHour hs[6]; const int nh = weatherHours(hs, 6);
  lock(); const WxNow n = now_; float q[8]; memcpy(q, quarter, sizeof(q)); const int nq = quarterCount; const time_t q0 = quarter0; unlock();
  const time_t now = time(nullptr);
  // Trovoadas nas proximas 3 horas
  for(int i = 0; i < nh && i < 4; i++)
    if(hs[i].code >= 95 && n.code < 95)
    {
      struct tm t; localtime_r(&hs[i].t, &t);
      snprintf(title, tl, "Trovoadas às %dh", t.tm_hour);
      snprintf(sub, sl, "%d%% de chance · fique atento", hs[i].prob);
      return true;
    }
  if(!rainy(n.code) && n.precip < 0.1f)
  {
    // Chuva chegando (dados de 15 em 15 min)
    for(int i = 0; i < nq; i++)
    {
      const time_t at = q0 + i * 900;
      if(at < now - 600 || at > now + 7200 || q[i] < 0.2f) continue;
      const int mins = max(5, (int)((at - now) / 60));
      float tot = 0; for(int k = i; k < nq; k++) tot += q[k];
      snprintf(title, tl, mins <= 10 ? T("Rain coming","Chuva chegando","Lluvia llegando") : T("Rain in %d min","Chuva em %d min","Lluvia en %d min"), (mins + 4) / 5 * 5);
      snprintf(sub, sl, "Feche as janelas · %.0f mm", max(1.0f, tot));
      return true;
    }
    for(int i = 1; i < nh && i < 3; i++)
      if(hs[i].prob >= 70)
      {
        struct tm t; localtime_r(&hs[i].t, &t);
        snprintf(title, tl, T("Rain at %dh","Chuva às %dh","Lluvia a las %dh"), t.tm_hour);
        snprintf(sub, sl, "%d%% de chance", hs[i].prob);
        return true;
      }
  }
  else
  {
    // Esta chovendo: avisa quando para
    for(int i = 0; i < nq; i++)
    {
      const time_t at = q0 + i * 900;
      if(at < now || at > now + 7200 || q[i] >= 0.1f) continue;
      struct tm t; localtime_r(&at, &t);
      strlcpy(title, "Chovendo agora", tl);
      snprintf(sub, sl, "Deve parar por volta de %02d:%02d", t.tm_hour, t.tm_min);
      return true;
    }
  }
  return false;
}

static const char *const WD[3][7] = {{"Sun","Mon","Tue","Wed","Thu","Fri","Sat"},{"Dom","Seg","Ter","Qua","Qui","Sex","Sáb"},{"Dom","Lun","Mar","Mié","Jue","Vie","Sáb"}};

// ---- Icones desenhados (suavizados, qualquer tamanho) ----
static void cloud(float cx, float cy, float s, uint32_t col)
{
  canvas.fillSmoothCircle(cx - s * 0.28f, cy + s * 0.02f, s * 0.24f, col);
  canvas.fillSmoothCircle(cx + s * 0.05f, cy - s * 0.12f, s * 0.32f, col);
  canvas.fillSmoothCircle(cx + s * 0.34f, cy + s * 0.05f, s * 0.22f, col);
  canvas.fillSmoothRoundRect(cx - s * 0.52f, cy + s * 0.02f, s * 1.08f, s * 0.24f, s * 0.12f, col);
}

static void sun(float cx, float cy, float s)
{
  const uint32_t col = 0xFFC94Au;
  for(int i = 0; i < 8; i++)
  {
    const float a = i * 45 * DEG_TO_RAD;
    canvas.drawWideLine(cx + cosf(a) * s * 0.42f, cy + sinf(a) * s * 0.42f, cx + cosf(a) * s * 0.58f, cy + sinf(a) * s * 0.58f, s * 0.045f, col);
  }
  canvas.fillSmoothCircle(cx, cy, s * 0.3f, col);
}

static void moon(float cx, float cy, float s, uint32_t bg)
{
  canvas.fillSmoothCircle(cx, cy, s * 0.32f, 0xE6E9F0u);
  canvas.fillSmoothCircle(cx + s * 0.16f, cy - s * 0.1f, s * 0.27f, bg);
}

static void drops(float cx, float cy, float s, int n, uint32_t col, bool snow)
{
  for(int i = 0; i < n; i++)
  {
    const float x = cx - s * 0.3f + i * (s * 0.6f / max(1, n - 1));
    const float y = cy + s * 0.36f + (i & 1) * s * 0.08f;
    if(snow) canvas.fillSmoothCircle(x, y + s * 0.06f, s * 0.05f, 0xFFFFFFu);
    else canvas.drawWideLine(x, y, x - s * 0.06f, y + s * 0.16f, s * 0.035f, col);
  }
}

void weatherIcon(float cx, float cy, float s, int code, bool day, uint32_t bg)
{
  const uint32_t light = 0xDDE6EAu, dark = 0x8E9AA3u;
  auto bodyLight = [&]() { if(day) sun(cx - s * 0.18f, cy - s * 0.16f, s * 0.75f); else moon(cx - s * 0.16f, cy - s * 0.18f, s * 0.8f, bg); };
  switch(code)
  {
    case 0: if(day) sun(cx, cy, s); else moon(cx, cy, s, bg); break;
    case 1: case 2: bodyLight(); cloud(cx + s * 0.1f, cy + s * 0.1f, s * (code == 1 ? 0.62f : 0.8f), light); break;
    case 3: cloud(cx + s * 0.12f, cy - s * 0.1f, s * 0.6f, dark); cloud(cx - s * 0.05f, cy + s * 0.08f, s * 0.85f, light); break;
    case 45: case 48:
      cloud(cx, cy - s * 0.12f, s * 0.8f, dark);
      for(int i = 0; i < 3; i++) canvas.drawWideLine(cx - s * 0.42f + i * 6, cy + s * 0.2f + i * s * 0.12f, cx + s * 0.42f, cy + s * 0.2f + i * s * 0.12f, s * 0.03f, light);
      break;
    case 51: case 53: case 55: case 56: case 57:
      cloud(cx, cy - s * 0.12f, s * 0.85f, light); drops(cx, cy - s * 0.1f, s, 3, 0x6EC6FFu, false); break;
    case 61: case 63: case 65: case 66: case 67:
      cloud(cx, cy - s * 0.12f, s * 0.85f, code == 65 ? dark : light); drops(cx, cy - s * 0.1f, s, code == 61 ? 3 : 5, 0x4FA9FFu, false); break;
    case 80: case 81: case 82:
      bodyLight(); cloud(cx + s * 0.08f, cy, s * 0.8f, light); drops(cx + s * 0.08f, cy + s * 0.02f, s * 0.9f, 3, 0x4FA9FFu, false); break;
    case 71: case 73: case 75: case 77: case 85: case 86:
      cloud(cx, cy - s * 0.12f, s * 0.85f, light); drops(cx, cy - s * 0.1f, s, 4, 0, true); break;
    case 95: case 96: case 99:
    {
      cloud(cx, cy - s * 0.14f, s * 0.9f, dark);
      const float x = cx + s * 0.02f, y = cy + s * 0.12f;
      canvas.fillTriangle(x, y, x - s * 0.16f, y + s * 0.24f, x + s * 0.02f, y + s * 0.22f, 0xFFD24Au);
      canvas.fillTriangle(x + s * 0.02f, y + s * 0.18f, x - s * 0.04f, y + s * 0.42f, x + s * 0.14f, y + s * 0.16f, 0xFFD24Au);
      break;
    }
    default: cloud(cx, cy, s * 0.8f, light);
  }
}

// ---- Rede ----
static void fetchWeather()
{
  double wLat, wLon; featWeatherCenter(wLat, wLon, nullptr);
  crumbWorker("clima:baixar");
  char url[700];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,precipitation,weather_code,cloud_cover,pressure_msl,wind_speed_10m,wind_direction_10m,wind_gusts_10m"
           "&hourly=temperature_2m,precipitation_probability,precipitation,weather_code,is_day"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,precipitation_sum,sunrise,sunset"
           "&minutely_15=precipitation"
           "&timezone=auto&forecast_days=7&forecast_hours=24&forecast_minutely_15=8&timeformat=unixtime",
           wLat, wLon);
  HttpBuf buf; char err[48];
  if(httpGet(url, buf, 64 * 1024, err, sizeof(err), "application/json") != 200)
  {
    lock(); snprintf(wxStatus, sizeof(wxStatus), T("Weather: %s","Clima: %s","Clima: %s"), err); unlock();
    wxFetchAt = millis() + 120000;
    return;
  }
  crumbWorker("clima:json");
  JsonDocument doc(SpiRamAllocator::instance());
  if(deserializeJson(doc, buf.str(), buf.len)) { lock(); strlcpy(wxStatus, T("Weather: invalid response","Clima: resposta inválida","Clima: respuesta inválida"), sizeof(wxStatus)); unlock(); wxFetchAt = millis() + 120000; return; }
  buf.release();
  JsonObjectConst c = doc["current"];
  WxNow n; memset(&n, 0, sizeof(n));
  n.temp = c["temperature_2m"] | 0.0f; n.feels = c["apparent_temperature"] | 0.0f; n.precip = c["precipitation"] | 0.0f;
  n.wind = c["wind_speed_10m"] | 0.0f; n.gusts = c["wind_gusts_10m"] | 0.0f; n.pressure = c["pressure_msl"] | 0.0f;
  n.hum = c["relative_humidity_2m"] | 0; n.cloud = c["cloud_cover"] | 0; n.code = c["weather_code"] | 0;
  n.windDir = c["wind_direction_10m"] | 0; n.day = (c["is_day"] | 1) == 1;
  n.at = time(nullptr);

  JsonObjectConst d = doc["daily"];
  const long utcOff = doc["utc_offset_seconds"] | 0L;
  static WxDay ds[MAX_DAYS]; int nd = 0;
  for(int i = 0; i < MAX_DAYS && i < (int)d["time"].size(); i++)
  {
    ds[i].date = d["time"][i].as<long>() + utcOff;
    ds[i].code = d["weather_code"][i] | 0;
    ds[i].tmax = d["temperature_2m_max"][i] | 0.0f;
    ds[i].tmin = d["temperature_2m_min"][i] | 0.0f;
    ds[i].rainProb = d["precipitation_probability_max"][i] | 0;
    ds[i].mm = d["precipitation_sum"][i] | 0.0f;
    if(i == 0)
    {
      n.sunrise = d["sunrise"][0].as<long>(); n.sunset = d["sunset"][0].as<long>();
      n.tmax = ds[0].tmax; n.tmin = ds[0].tmin; n.rainProb = ds[0].rainProb;
    }
    nd++;
  }

  JsonObjectConst h = doc["hourly"];
  static WxHour hs[MAX_HOURS]; int nh = 0;
  for(int i = 0; i < MAX_HOURS && i < (int)h["time"].size(); i++)
  {
    hs[i].t = h["time"][i].as<long>();
    hs[i].temp = h["temperature_2m"][i] | 0.0f;
    hs[i].prob = h["precipitation_probability"][i] | 0;
    hs[i].mm = h["precipitation"][i] | 0.0f;
    hs[i].code = h["weather_code"][i] | 0;
    hs[i].day = (h["is_day"][i] | 1) == 1;
    nh++;
  }

  JsonObjectConst m = doc["minutely_15"];
  float q[8] = {0}; int nq = 0; time_t q0 = 0;
  for(int i = 0; i < 8 && i < (int)m["time"].size(); i++)
  {
    if(i == 0) q0 = m["time"][0].as<long>();
    q[i] = m["precipitation"][i] | 0.0f;
    nq++;
  }

  lock();
  now_ = n; memcpy(days, ds, sizeof(WxDay) * nd); dayCount = nd;
  memcpy(hours, hs, sizeof(WxHour) * nh); hourCount = nh;
  memcpy(quarter, q, sizeof(q)); quarterCount = nq; quarter0 = q0;
  haveWx = true; wxTime = time(nullptr); wxStatus[0] = 0;
  unlock();
  wxFetchAt = millis() + 15 * 60000UL;
  wxGen = featGen;
}

// falha passageira: mostra "Carregando" e tenta logo; so depois de 3 seguidas mostra o motivo
static void radarFail(const char *why, uint32_t retryMs)
{
  radarFails++;
  lock();
  if(radarFails >= 3 || frameCount) snprintf(radarStatus, sizeof(radarStatus), T("Radar: %s","Radar: %s","Radar: %s"), why);
  else strlcpy(radarStatus, T("Loading radar…","Carregando o radar…","Cargando radar…"), sizeof(radarStatus));
  unlock();
  radarFetchAt = millis() + (frameCount ? retryMs : min(retryMs, (uint32_t)(radarFails < 3 ? 8000 : 30000)));
}

static void fetchRadar()
{
  HttpBuf buf; char err[48];
  if(httpGet("https://api.rainviewer.com/public/weather-maps.json", buf, 16 * 1024, err, sizeof(err), "application/json") != 200)
  { radarFail(err, 120000); return; }
  JsonDocument doc;
  if(deserializeJson(doc, buf.str(), buf.len)) { radarFetchAt = millis() + 120000; return; }
  const char *host = doc["host"] | "https://tilecache.rainviewer.com";
  JsonArrayConst past = doc["radar"]["past"];
  const int total = past.size();
  if(!total) { lock(); strlcpy(radarStatus, T("Radar: no data","Radar sem dados","Radar sin datos"), sizeof(radarStatus)); unlock(); radarFetchAt = millis() + 120000; return; }
  const uint32_t newest = past[total - 1]["time"] | 0;
  double wLat, wLon; featWeatherCenter(wLat, wLon, nullptr);
  const uint8_t view = featViewNow(feat.wStyle, wLat, wLon);
  if(newest == radarTimeStampSeen && radarGen == featGen && view == radarView && frameCount) { radarFetchAt = millis() + 120000; return; }

  // Mapa base no zoom do radar, numa area de 512 px do mundo (o tamanho da imagem do radar)
  const int z = feat.wZoom;
  const double tileMpp = mapMetersPerPixel(wLat, z);
  MapCanvas mc; char merr[64];
  crumbWorker("radar:mapa");
  if(!mapPrepare(mc, wLat, wLon, tileMpp * 512.0 / LCD_W, featMapStyle(view), merr, sizeof(merr), z))
  { radarFail(merr, 60000); return; }
  merr[0] = 0;
  if(featMapNeedsDim(view)) mapTint((uint16_t *)mc.big->getBuffer(), 90, 0x00140Fu, mc.n * mc.n);

  LGFX_Sprite *workS = mapBigSprite(mc.n);
  if(!workS) { lock(); strlcpy(radarStatus, T("Radar: out of memory","Radar: sem memória","Radar: sin memoria"), sizeof(radarStatus)); unlock(); radarFetchAt = millis() + 60000; return; }
  LGFX_Sprite &work = *workS;

  const int want = min((int)feat.wFrames, min(total, MAX_FRAMES));
  int made = 0;
  for(int k = total - want; k < total; k++)
  {
    const char *path = past[k]["path"] | "";
    const uint32_t t = past[k]["time"] | 0;
    char url[200];
    snprintf(url, sizeof(url), "%s%s/512/%d/%.4f/%.4f/2/1_0.png", host, path, z, wLat, wLon);
    char cr[24]; snprintf(cr, sizeof(cr), "radar:quadro %d", made + 1); crumbWorker(cr);
    if(httpGet(url, buf, 400 * 1024, err, sizeof(err), "image/png") != 200) { strlcpy(merr, err, sizeof(merr)); continue; }
    memcpy(work.getBuffer(), mc.big->getBuffer(), mc.n * mc.n * 2);
    work.drawPng(buf.data, buf.len, (mc.n - 512) / 2, (mc.n - 512) / 2);
    if(!frames[made]) frames[made] = (uint16_t *)heap_caps_malloc(LCD_W * LCD_H * 2, MALLOC_CAP_SPIRAM);
    if(!frames[made]) break;
    // troca o quadro sem atrapalhar a animacao: grava num temporario e copia sob trava
    static uint16_t *tmp = nullptr;
    if(!tmp) tmp = (uint16_t *)heap_caps_malloc(LCD_W * LCD_H * 2, MALLOC_CAP_SPIRAM);
    if(!tmp) break;
    mapDownsample(mc, (const uint16_t *)work.getBuffer(), tmp);
    lock();
    memcpy(frames[made], tmp, LCD_W * LCD_H * 2);
    frameTime[made] = t;
    if(made + 1 > frameCount) frameCount = made + 1;
    unlock();
    made++;
    vTaskDelay(pdMS_TO_TICKS(150));   // deixa o Wi-Fi e a tela respirarem entre os quadros
  }
  mapBigRelease(workS);
  buf.release();
  lock();
  frameCount = made;
  if(made) radarStatus[0] = 0;
  unlock();
  if(!made) { radarFail(merr[0] ? merr : "sem quadros", 60000); return; }
  radarFails = 0;
  radarTimeStampSeen = newest;
  radarGen = featGen;
  radarView = view;
  radarFetchAt = millis() + 10 * 60000UL;
}

void weatherBegin()
{
  mtx = xSemaphoreCreateMutex();
}

void weatherWork()
{
  if(!netConnected() || !netTimeValid()) return;
  const uint8_t cur = uiCurrent();
  if((!haveWx || wxGen != featGen || (int32_t)(millis() - wxFetchAt) >= 0)) fetchWeather();
  // Radar so e montado com a tela aberta (ou logo antes), para nao gastar internet a toa
  // configuracao mudou: refaz ja (mas uma falha nao vira repeticao sem pausa)
  static uint32_t genAsked = 0;
  if(genAsked != featGen) { genAsked = featGen; radarFetchAt = 0; radarFails = 0; }
  bool viewChanged = false;
  if(feat.wStyle == VIEW_AUTO && frameCount)
  {
    static uint32_t checkAt = 0;
    if((int32_t)(millis() - checkAt) >= 0)
    {
      checkAt = millis() + 60000;
      double la, lo; featWeatherCenter(la, lo, nullptr);
      viewChanged = featViewNow(VIEW_AUTO, la, lo) != radarView;
    }
  }
  if(feat.scrWxRadar && cur == SCR_WX_RADAR && (viewChanged || (int32_t)(millis() - radarFetchAt) >= 0)) fetchRadar();
}

#ifdef R360_SIM
// So no simulador do PC: coloca um quadro de radar pronto (sem internet)
void weatherSimRadar(const uint16_t *img, uint8_t view)
{
  if(!frames[0]) frames[0] = (uint16_t *)heap_caps_malloc(LCD_W * LCD_H * 2, MALLOC_CAP_SPIRAM);
  memcpy(frames[0], img, LCD_W * LCD_H * 2); frameTime[0] = time(nullptr) - 600; frameCount = 1;
  radarView = view; radarGen = featGen; radarFetchAt = millis() + 600000; radarTimeStampSeen = 1;
}
#endif

bool weatherRadarAnimated() { return frameCount > 1; }
bool weatherRadarCardAnimated() { return frameCount > 0 && cardsShown && feat.wCards && haveWx && radarCardAnimated(); }
void weatherRadarLongPress() { cardsShown = !cardsShown; }

static void needData(const char *msg)
{
  gfxBackground();
  gfxStatusLine(msg, C_MUTED);
}

static void fmtTemp(char *b, size_t n, float t)
{
  snprintf(b, n, "%d°", (int)lroundf(t));
}

static uint32_t mixc(uint32_t a, uint32_t b, float t)
{
  t = constrain(t, 0.0f, 1.0f);
  const int r = ((a >> 16) & 255) + (((int)((b >> 16) & 255) - (int)((a >> 16) & 255)) * t);
  const int g = ((a >> 8) & 255) + (((int)((b >> 8) & 255) - (int)((a >> 8) & 255)) * t);
  const int bl = (a & 255) + (((int)(b & 255) - (int)(a & 255)) * t);
  return (r << 16) | (g << 8) | bl;
}

// cor da temperatura: frio azul, ameno dourado, quente coral
static uint32_t tempColor(float t)
{
  if(t < 22) return mixc(0x7FD8FFu, 0xFFC857u, (t - 12) / 10.0f);
  return mixc(0xFFC857u, 0xFF6B6Bu, (t - 22) / 12.0f);
}

bool weatherNowAnimated() { return feat.wNowStyle == 0 && skyFrameMs() > 0; }

// ---- Entrada animada das telas do clima (0,8 s) ----
static uint32_t enterAt = 0;
void weatherOpen() { enterAt = millis(); }
static float intro()
{
  const float t = constrain((millis() - enterAt) / 800.0f, 0.0f, 1.0f);
  return 1 - (1 - t) * (1 - t) * (1 - t);   // desacelera no fim
}
bool weatherIntroRunning() { return millis() - enterAt < 850; }

// Nome do lugar que caiba na largura: tira "Aeroporto de", "Municipio de"... se precisar
static void placeLabel(char *out, size_t n, int maxW, const uint8_t *font)
{
  strlcpy(out, weatherPlace(), n);
  if(gfxTextWidth(out, font) <= maxW) return;
  static const char *const PREFIX[] = {"Aeroporto Internacional de ", "Aeroporto Internacional ", "Aeroporto de ", "Aeroporto do ", "Aeroporto da ",
                                       "Aeroporto ", "Município de ", "Cidade de ", "Distrito de "};
  for(const char *p : PREFIX)
    if(!strncasecmp(out, p, strlen(p))) { memmove(out, out + strlen(p), strlen(out) - strlen(p) + 1); break; }
  // corta depois de virgula ("Cidade, Estado")
  if(gfxTextWidth(out, font) > maxW) { char *c = strchr(out, ','); if(c) *c = 0; }
}

static void drawNowSimple(const WxNow &n)
{
  gfxBackground();
  char pl[64]; placeLabel(pl, sizeof(pl), 210, FontSmall);
  gfxTextFit(pl, CX, 44, 210, FontSmall, C_MUTED);
  weatherIcon(CX, 106, 80, n.code, n.day, C_BG);
  char b[48];
  fmtTemp(b, sizeof(b), n.temp);
  gfxText(b, CX + 8, 196, FontClock, C_TEXT);
  gfxTextFit(weatherCondition(n.code), CX, 258, 260, FontBody, C_ACCENT);
  snprintf(b, sizeof(b), T("max %d°  ·  min %d°","máx %d°  ·  mín %d°","máx %d°  ·  mín %d°"), (int)lroundf(n.tmax), (int)lroundf(n.tmin));
  gfxText(b, CX, 286, FontSmall, C_TEXT);
  snprintf(b, sizeof(b), T("Feels %d°  ·  %d%%  ·  %d km/h","Sensação %d°  ·  %d%%  ·  %d km/h","Sensación %d°  ·  %d%%  ·  %d km/h"), (int)lroundf(n.feels), n.hum, (int)lroundf(n.wind));
  gfxTextFit(b, CX, 312, 250, FontSmall, C_MUTED);
}

void weatherDrawNow()
{
  WxNow n;
  if(!weatherNow(&n)) { char s[64]; lock(); strlcpy(s, wxStatus, sizeof(s)); unlock(); needData(s); return; }
  if(feat.wNowStyle == 1) { drawNowSimple(n); return; }

  // Realista: ceu da cena atual, textos com sombra
  skyDraw(true);
  char b[64];
  const float e = intro();
  char pl[64]; placeLabel(pl, sizeof(pl), 2 * gfxRoundHalfWidth(56, 16), FontSmall);
  gfxTextShadowFit(pl, CX, 56, 2 * gfxRoundHalfWidth(56, 16), FontSmall, 0xFFFFFFu);
  fmtTemp(b, sizeof(b), n.temp - 9 * (1 - e));   // conta ate a temperatura
  gfxTextShadow(b, CX + 10, 122, FxClockXL, 0xFFFFFFu);
  gfxTextShadow(weatherCondition(n.code), CX, 184, FontBody, 0xFFFFFFu);
  snprintf(b, sizeof(b), T("Max %d°   Min %d°","Máx %d°   Mín %d°","Máx %d°   Mín %d°"), (int)lroundf(n.tmax), (int)lroundf(n.tmin));
  gfxTextShadow(b, CX, 210, FontSmall, 0xE6ECF2u);
  char t1[40], t2[48];
  if(weatherAlert(t1, sizeof(t1), t2, sizeof(t2))) gfxTextShadow(t1, CX, 236, FontSmall, 0xBFD8FFu);
  else { snprintf(b, sizeof(b), T("Feels %d°  ·  Humidity %d%%","Sensação %d°  ·  Umidade %d%%","Sensación %d°  ·  Humedad %d%%"), (int)lroundf(n.feels), n.hum); gfxTextShadow(b, CX, 236, FontSmall, 0xE6ECF2u); }

  // faixa de vidro com as proximas horas
  WxHour hs[6]; const int nh = weatherHours(hs, 5);
  if(nh >= 2)
  {
    const int cols = min(nh, 5), cw = 50, w = cols * cw + 10, x0 = CX - w / 2, y0 = 258 + (int)(70 * (1 - e)), h = 58;
    gfxBlendRoundRect(x0, y0, w, h, 18, 0x0A1420u, 105, 0xFFFFFFu, 60);
    for(int i = 0; i < cols; i++)
    {
      const int x = x0 + 5 + cw / 2 + i * cw;
      if(i == 0) strlcpy(b, "Agora", sizeof(b));
      else { struct tm t; localtime_r(&hs[i].t, &t); snprintf(b, sizeof(b), "%dh", t.tm_hour); }
      gfxText(b, x, y0 + 15, FontSmall, hs[i].prob >= 40 && i ? 0x9CC8FFu : 0xD8E2ECu);
      fmtTemp(b, sizeof(b), i == 0 ? n.temp : hs[i].temp);
      gfxText(b, x, y0 + 39, FontBody, 0xFFFFFFu);
    }
  }
}

#include "WxThemes.inc"

void weatherDrawHours()
{
  WxNow n;
  if(!weatherNow(&n)) { char s[64]; lock(); strlcpy(s, wxStatus, sizeof(s)); unlock(); needData(s); return; }
  WxHour hs[13]; const int nh = weatherHours(hs, 13);
  if(feat.wHoursStyle == 1 && nh >= 3) { drawHoursSky(n, hs, nh); return; }
  if(feat.wHoursStyle == 2 && nh >= 5) { drawHoursCards(n, hs, nh); return; }
  if(!gfxImage("bg_obsidian")) gfxBackground();
  gfxText(T("Next 12 hours","Próximas 12 horas","Próximas 12 horas"), CX, 46, FontSmall, C_MUTED);
  char b[64];
  weatherIcon(CX - 46, 92, 54, n.code, n.day, 0x0A1A18u);
  fmtTemp(b, sizeof(b), n.temp);
  gfxText(b, CX - 10, 90, FxLight30, C_TEXT, middle_left);
  gfxTextFit(weatherConditionShort(n.code), CX - 10, 116, 120, FontSmall, C_MUTED, middle_left);
  if(nh < 3) return;

  const int N = min(nh, 13);
  float lo = 99, hi = -99;
  for(int i = 0; i < N; i++) { lo = min(lo, hs[i].temp); hi = max(hi, hs[i].temp); }
  if(hi - lo < 4) { const float m = (hi + lo) / 2; lo = m - 2; hi = m + 2; }
  const float X0 = 68, X1 = 292, YT = 160, YB = 222, BASE = 250;
  auto px = [&](int i) { return X0 + i * (X1 - X0) / (N - 1); };
  auto py = [&](float t) { return YB - (t - lo) / (hi - lo) * (YB - YT); };
  const float e = intro();
  const float XL = X0 + (X1 - X0) * e;   // ate onde a curva ja foi desenhada

  // area sob a curva (degrade)
  uint16_t *buf = (uint16_t *)canvas.getBuffer();
  for(int x = (int)X0; x <= (int)XL; x++)
  {
    const float f = (x - X0) / (X1 - X0) * (N - 1);
    const int i = min(N - 2, (int)f); const float fr = f - i;
    const float y = py(hs[i].temp + (hs[i + 1].temp - hs[i].temp) * fr);
    const uint32_t c = tempColor(hs[i].temp + (hs[i + 1].temp - hs[i].temp) * fr);
    for(int yy = (int)y; yy < BASE; yy++)
    {
      const uint8_t a = (uint8_t)(110 * (1 - (yy - y) / (BASE - y)));
      uint16_t &p = buf[yy * LCD_W + x]; p = gfxBlend(p, c, a);
    }
  }
  // chuva (probabilidade) em barras
  for(int i = 0; i < N; i++)
    if(hs[i].prob >= 10)
    {
      const float h = hs[i].prob * 0.28f * e;
      canvas.fillSmoothRoundRect(px(i) - 4, 282 - h, 8, h, 3, hs[i].prob >= 50 ? 0x5AA9FFu : 0x2F5E8Au);
    }
  canvas.drawFastHLine(X0 - 6, 283, X1 - X0 + 12, 0x1E4A42u);
  // curva
  for(int i = 0; i < N - 1; i++)
  {
    if(px(i) > XL) break;
    const float f = min(1.0f, (XL - px(i)) / (px(i + 1) - px(i)));
    const float x2 = px(i) + (px(i + 1) - px(i)) * f, y2 = py(hs[i].temp) + (py(hs[i + 1].temp) - py(hs[i].temp)) * f;
    canvas.drawWideLine(px(i), py(hs[i].temp), x2, y2, 1.6f, tempColor((hs[i].temp + hs[i + 1].temp) / 2));
  }
  for(int i = 0; i < N; i += 2)
  {
    if(px(i) > XL + 1) break;
    const float x = px(i), y = py(hs[i].temp);
    canvas.fillSmoothCircle(x, y, 5, tempColor(hs[i].temp));
    canvas.fillSmoothCircle(x, y, 3, 0x071311u);
    fmtTemp(b, sizeof(b), hs[i].temp);
    gfxText(b, x, y - 16, FontSmall, C_TEXT);
    struct tm t; localtime_r(&hs[i].t, &t);
    snprintf(b, sizeof(b), "%dh", t.tm_hour);
    gfxText(b, x, 298, FontSmall, C_MUTED);
  }
  // resumo da chuva
  int first = -1, last = -1; float tot = 0;
  for(int i = 0; i < N; i++) if(hs[i].prob >= 50 || hs[i].mm >= 0.3f) { if(first < 0) first = i; last = i; tot += hs[i].mm; }
  if(first >= 0)
  {
    struct tm a, z; localtime_r(&hs[first].t, &a); time_t te = hs[last].t + 3600; localtime_r(&te, &z);
    snprintf(b, sizeof(b), T("Rain %dh–%dh · %.0f mm","Chuva %dh–%dh · %.0f mm","Lluvia %dh–%dh · %.0f mm"), a.tm_hour, z.tm_hour, max(1.0f, tot));
    gfxTextFit(b, CX, 322, 210, FontSmall, 0x5AA9FFu);
  }
  else gfxText(T("No rain in sight","Sem chuva à vista","Sin lluvia a la vista"), CX, 322, FontSmall, C_ACCENT);
}

void weatherDrawDays()
{
  WxDay ds[MAX_DAYS]; const int nd = weatherDays(ds, MAX_DAYS);
  if(!nd) { char s[64]; lock(); strlcpy(s, wxStatus, sizeof(s)); unlock(); needData(s); return; }
  if(feat.wDaysStyle == 1) { drawDaysColumns(ds, nd); return; }
  const bool themed = gfxImage("bg_aurora");
  if(!themed) gfxBackground();
  const uint32_t GOLD = 0xF2C66Du;
  canvas.drawCircle(CX, CY, 170, 0x6E5A3Au);
  gfxTextSpaced(T("NEXT DAYS","PRÓXIMOS DIAS","PRÓXIMOS DÍAS"), CX, 54, FxCinzel14, GOLD, 3);
  const int rows = min(nd, 5);
  float lo = 99, hi = -99;
  for(int i = 0; i < rows; i++) { lo = min(lo, ds[i].tmin); hi = max(hi, ds[i].tmax); }
  if(hi - lo < 6) hi = lo + 6;
  int wettest = -1;
  char b[48];
  for(int i = 0; i < rows; i++)
  {
    const int y = 94 + i * 42;
    struct tm t; gmtime_r(&ds[i].date, &t);
    const char *label = i == 0 ? T("Today","Hoje","Hoy") : i == 1 ? T("Tomorrow","Amanhã","Mañana") : WD[uiLang][t.tm_wday];
    gfxText(label, 108, y - 1, FxSerif22, 0xF6F3FFu, middle_right);
    weatherIcon(132, y, 30, ds[i].code, true, 0x0C1033u);
    snprintf(b, sizeof(b), "%d°", (int)lroundf(ds[i].tmin));
    gfxText(b, 178, y, FontSmall, 0xB9B4DEu, middle_right);
    // barra minima-maxima em degrade
    const int bx0 = 186, bw = 96;
    canvas.fillSmoothRoundRect(bx0, y - 3, bw, 6, 3, 0x2A2560u);
    const float e = constrain(intro() * 1.6f - i * 0.15f, 0.0f, 1.0f);   // uma linha depois da outra
    const int a0 = bx0 + (int)((ds[i].tmin - lo) / (hi - lo) * bw);
    const int a1 = a0 + (int)(((ds[i].tmax - lo) / (hi - lo) * bw - (a0 - bx0)) * e);
    for(int x = a0; x <= a1; x++) canvas.drawFastVLine(x, y - 3, 6, mixc(0x7FE3FFu, GOLD, (float)(x - bx0) / bw));
    canvas.fillSmoothCircle(a0, y, 3, 0x7FE3FFu); canvas.fillSmoothCircle(a1, y, 3, mixc(0x7FE3FFu, GOLD, (float)(a1 - bx0) / bw));
    snprintf(b, sizeof(b), "%d°", (int)lroundf(ds[i].tmax));
    gfxText(b, 290, y, FontSmall, 0xFFFFFFu, middle_left);
    if(ds[i].rainProb >= 50 && (wettest < 0 || ds[i].rainProb > ds[wettest].rainProb)) wettest = i;
  }
  if(wettest >= 0)
  {
    struct tm t; gmtime_r(&ds[wettest].date, &t);
    static const char *const FULL[3][7] = {{"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"},{"Domingo","Segunda","Terça","Quarta","Quinta","Sexta","Sábado"},{"Domingo","Lunes","Martes","Miércoles","Jueves","Viernes","Sábado"}};
    snprintf(b, sizeof(b), T("%s: %d%% rain","%s: %d%% de chuva","%s: %d%% de lluvia"), wettest == 0 ? T("Today","Hoje","Hoy") : wettest == 1 ? T("Tomorrow","Amanhã","Mañana") : FULL[uiLang][t.tm_wday], ds[wettest].rainProb);
    gfxTextFit(b, CX, 306, 220, FxSerif22, GOLD);
  }
  else gfxText(T("No heavy rain this week","Semana sem chuva forte","Semana sin lluvia fuerte"), CX, 306, FxSerif22, GOLD);
}

void weatherDrawRadar()
{
  lock();
  const int nf = frameCount;
  if(!nf)
  {
    char s[64]; strlcpy(s, radarStatus, sizeof(s)); unlock();
    needData(s);
    return;
  }
  // Ultimo quadro fica o dobro do tempo
  const uint32_t ms = feat.wFrameMs;
  const uint32_t cycle = ms * (nf + 1);
  int idx = (millis() % cycle) / ms; if(idx >= nf) idx = nf - 1;
  memcpy(canvas.getBuffer(), frames[idx], LCD_W * LCD_H * 2);
  const time_t ft = frameTime[idx];
  unlock();

  // Sua posicao
  const uint32_t me = radarView == VIEW_BRIGHT ? 0x0F5A4Cu : 0xFFFFFFu;
  if(radarView == VIEW_BRIGHT) canvas.fillSmoothCircle(CX, CY, 7, 0xFFFFFFu);
  canvas.fillSmoothCircle(CX, CY, 5, me);
  canvas.drawCircle(CX, CY, 9, me);

  // Hora do quadro e progresso
  struct tm t; localtime_r(&ft, &t);
  char b[48]; snprintf(b, sizeof(b), "%02d:%02d", t.tm_hour, t.tm_min);
  canvas.fillSmoothRoundRect(CX - 44, 30, 88, 30, 15, 0x0B1716u);
  gfxText(b, CX, 45, FontBody, C_TEXT);
  const int gap = 12, x0 = CX - (nf - 1) * gap / 2;
  for(int i = 0; i < nf; i++) canvas.fillSmoothCircle(x0 + i * gap, 72, i == idx ? 4 : 2, i == idx ? C_ACCENT : 0x6C8C86u);

  if(cardsShown && feat.wCards && haveWx)
  {
    lock(); const WxNow n = now_; unlock();
    radarCardDraw(n);
  }
  const bool up = cardsShown && feat.wCards && haveWx && radarCardCoversBottom();
  gfxText("RainViewer", CX, up ? 92 : 342, FxInter13, radarView == VIEW_BRIGHT ? 0x3E5550u : 0x8FB0A8u);
}
