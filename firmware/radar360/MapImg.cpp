#include "MapImg.h"
#include "Lang.h"
#include "Http.h"
#include "Config.h"
#include <LittleFS.h>
#include <math.h>

static const char *CACHE_DIR = "/tc";
static const size_t TILE_MAX = 160 * 1024;

// Imagens grandes (600-900 KB) guardadas para reuso: apagar e pedir de novo a cada atualizacao do mapa
// fragmentava a PSRAM e, depois de um tempo, nao havia mais um bloco inteiro desse tamanho ("sem memoria").
static LGFX_Sprite *pool[1] = {nullptr};
static SemaphoreHandle_t poolMtx = nullptr;
static void poolLock() { if(!poolMtx) poolMtx = xSemaphoreCreateMutex(); xSemaphoreTake(poolMtx, portMAX_DELAY); }
static void poolUnlock() { xSemaphoreGive(poolMtx); }

LGFX_Sprite *mapBigSprite(int n)
{
  poolLock();
  LGFX_Sprite *s = nullptr;
  for(auto &p : pool) if(p && p->width() == n) { s = p; p = nullptr; break; }
  if(!s) for(auto &p : pool) if(p) { s = p; p = nullptr; break; }   // outro tamanho: reaproveita o objeto
  poolUnlock();
  if(s && s->width() == n) return s;
  if(s) s->deleteSprite();
  else s = new LGFX_Sprite();
  s->setPsram(true);
  s->setColorDepth(16);
  if(!s->createSprite(n, n)) { delete s; return nullptr; }
  return s;
}

void mapBigTrim()
{
  poolLock();
  LGFX_Sprite *s = pool[0]; pool[0] = nullptr;
  poolUnlock();
  if(s) { s->deleteSprite(); delete s; }
}

void mapBigRelease(LGFX_Sprite *s)
{
  if(!s) return;
  poolLock();
  for(auto &p : pool) if(!p) { p = s; s = nullptr; break; }
  poolUnlock();
  if(s) { s->deleteSprite(); delete s; }
}

MapCanvas::~MapCanvas() { free(); }
void MapCanvas::free()
{
  mapBigRelease(big); big = nullptr;
  n = 0;
}

uint32_t mapBgColor(uint8_t style)
{
  switch(style)
  {
    case MAP_DARK: return 0x0E1013u;
    case MAP_LIGHT: return 0xF5F3EEu;
    case MAP_OSM: return 0xF2EFE9u;
    default: return 0x050B0Au;
  }
}

const char *mapAttribution(uint8_t style)
{
  return style == MAP_OSM ? "© OpenStreetMap" : style ? "© OpenStreetMap © CARTO" : "";
}

double mapMetersPerPixel(double lat, int z)
{
  return 156543.03392 * cos(lat * M_PI / 180.0) / (double)(1UL << z);
}

static void tileUrl(char *out, size_t n, uint8_t style, int z, int x, int y)
{
  static const char sub[] = "abcd";
  const char s = sub[(x + y) & 3];
  switch(style)
  {
    // CARTO exige chave (gratis) desde 2026: carto.com/basemaps/apikey
    case MAP_DARK:  snprintf(out, n, "https://basemaps.cartocdn.com/rastertiles/dark_all/%d/%d/%d.png?key=%s", z, x, y, cfg.cartoKey); break;
    case MAP_LIGHT: snprintf(out, n, "https://basemaps.cartocdn.com/rastertiles/voyager/%d/%d/%d.png?key=%s", z, x, y, cfg.cartoKey); break;
    default:        snprintf(out, n, "https://tile.openstreetmap.org/%d/%d/%d.png", z, x, y); break;
  }
}

// Cache de tiles na PSRAM (nao grava na flash enquanto a tela anima)
struct TileEntry { uint32_t key; uint8_t *data; size_t len; uint32_t used; };
static TileEntry tileCache[40];
static uint32_t tileClock = 0;
static SemaphoreHandle_t cacheMtx = nullptr;

static uint32_t tileKey(uint8_t style, int z, int x, int y) { return ((uint32_t)style << 28) ^ ((uint32_t)z << 23) ^ ((uint32_t)x << 11) ^ (uint32_t)y ^ ((uint32_t)x * 2654435761u); }

static bool cacheGet(uint32_t key, HttpBuf &buf)
{
  if(!cacheMtx) cacheMtx = xSemaphoreCreateMutex();
  xSemaphoreTake(cacheMtx, portMAX_DELAY);
  bool ok = false;
  for(auto &e : tileCache)
    if(e.data && e.key == key && buf.reserve(e.len)) { memcpy(buf.data, e.data, e.len); buf.len = e.len; e.used = ++tileClock; ok = true; break; }
  xSemaphoreGive(cacheMtx);
  return ok;
}

static void cachePut(uint32_t key, const HttpBuf &buf)
{
  if(!cacheMtx) cacheMtx = xSemaphoreCreateMutex();
  uint8_t *copy = (uint8_t *)heap_caps_malloc(buf.len, MALLOC_CAP_SPIRAM);
  if(!copy) return;
  memcpy(copy, buf.data, buf.len);
  xSemaphoreTake(cacheMtx, portMAX_DELAY);
  TileEntry *slot = &tileCache[0];
  for(auto &e : tileCache) { if(!e.data) { slot = &e; break; } if(e.used < slot->used) slot = &e; }
  if(slot->data) heap_caps_free(slot->data);
  slot->key = key; slot->data = copy; slot->len = buf.len; slot->used = ++tileClock;
  xSemaphoreGive(cacheMtx);
}

static volatile bool clearPending = false;
void mapCacheClearLater() { clearPending = true; }
void mapCacheWork() { if(clearPending) { clearPending = false; mapCacheClear(); } }

void mapCacheClear()
{
  if(cacheMtx)
  {
    xSemaphoreTake(cacheMtx, portMAX_DELAY);
    for(auto &e : tileCache) { if(e.data) heap_caps_free(e.data); e.data = nullptr; e.len = 0; }
    xSemaphoreGive(cacheMtx);
  }
  // Remove o cache antigo em arquivo (versao 0.2.0)
  File dir = LittleFS.open(CACHE_DIR);
  if(!dir) return;
  String names[64]; int n = 0;
  for(File f = dir.openNextFile(); f && n < 64; f = dir.openNextFile()) { names[n++] = String(CACHE_DIR) + "/" + f.name(); f.close(); }
  dir.close();
  for(int i = 0; i < n; i++) LittleFS.remove(names[i]);
  if(n == 64) mapCacheClear();
  // tambem apaga as imagens finais guardadas (pasta /mapf)
  File bd = LittleFS.open("/mapf");
  if(bd)
  {
    String bn[16]; int bnn = 0;
    for(File f = bd.openNextFile(); f && bnn < 16; f = bd.openNextFile()) { bn[bnn++] = String("/mapf/") + f.name(); f.close(); }
    bd.close();
    for(int i = 0; i < bnn; i++) LittleFS.remove(bn[i]);
  }
}

// ---- Cache da imagem final 360x360 no flash (so re-baixa quando muda local/zoom/estilo) ----
static const char *BASE_DIR = "/mapf";
static const size_t BASE_BYTES = (size_t)LCD_W * LCD_H * 2;

static void baseKeyPath(char *out, size_t n, double lat, double lon, double mpp, uint8_t style, uint8_t view)
{
  const long la = lround(lat * 10000), lo = lround(lon * 10000), mp = lround(mpp);
  const uint32_t h = (uint32_t)la * 2654435761u ^ (uint32_t)lo * 40503u ^ (uint32_t)mp * 2246822519u ^ ((uint32_t)style << 3) ^ view;
  snprintf(out, n, "%s/%08lx.b", BASE_DIR, (unsigned long)h);
}

// Mantem no maximo 8 imagens guardadas (LittleFS e dividido com as fotos): ao passar, limpa a pasta.
static void baseCap()
{
  File dir = LittleFS.open(BASE_DIR);
  if(!dir) return;
  int n = 0; String names[16];
  for(File f = dir.openNextFile(); f; f = dir.openNextFile()) { if(n < 16) names[n] = String(BASE_DIR) + "/" + f.name(); n++; f.close(); }
  dir.close();
  if(n > 8) for(int i = 0; i < n && i < 16; i++) LittleFS.remove(names[i]);
}

bool mapBaseLoad(double lat, double lon, double mpp, uint8_t style, uint8_t view, uint16_t *out)
{
  char p[40]; baseKeyPath(p, sizeof(p), lat, lon, mpp, style, view);
  File f = LittleFS.open(p, "r");
  if(!f) return false;
  bool ok = false;
  if(f.size() == BASE_BYTES) ok = f.read((uint8_t *)out, BASE_BYTES) == BASE_BYTES;
  f.close();
  return ok;
}

void mapBaseSave(double lat, double lon, double mpp, uint8_t style, uint8_t view, const uint16_t *img)
{
  if(!LittleFS.exists(BASE_DIR)) LittleFS.mkdir(BASE_DIR);
  char p[40]; baseKeyPath(p, sizeof(p), lat, lon, mpp, style, view);
  File f = LittleFS.open(p, "w");
  if(!f) return;
  const size_t wrote = f.write((const uint8_t *)img, BASE_BYTES);
  f.close();
  if(wrote != BASE_BYTES) { LittleFS.remove(p); return; }   // sem espaco: nao deixa arquivo pela metade
  baseCap();
}

void mapTint(uint16_t *buf, uint8_t keep, uint32_t add, size_t count)
{
  const int ar = (add >> 19) & 31, ag = (add >> 10) & 63, ab = (add >> 3) & 31;
  for(size_t i = 0; i < count; i++)
  {
    const uint16_t v = (uint16_t)(buf[i] >> 8 | buf[i] << 8);
    const int r = min(31, ((((v >> 11) & 31) * keep) >> 8) + ar);
    const int g = min(63, ((((v >> 5) & 63) * keep) >> 8) + ag);
    const int b = min(31, (((v & 31) * keep) >> 8) + ab);
    const uint16_t o = (uint16_t)((r << 11) | (g << 5) | b);
    buf[i] = (uint16_t)(o >> 8 | o << 8);
    if((i & 0x7FFF) == 0) vTaskDelay(0);
  }
}

bool mapPrepare(MapCanvas &mc, double lat, double lon, double mpp, uint8_t style, char *err, size_t errLen, int forcedZoom)
{
  if(err && errLen) err[0] = 0;
  const double cosLat = cos(lat * M_PI / 180.0);
  int z = forcedZoom >= 0 ? forcedZoom : (int)ceil(log2(156543.03392 * cosLat / mpp));
  z = constrain(z, 2, 16);
  const double tileMpp = 156543.03392 * cosLat / (double)(1UL << z);
  double s = mpp / tileMpp;
  if(s < 1) s = 1;   // nunca amplia
  const int n = (int)ceil(LCD_W * s) + 2;

  mc.free();
  mc.big = mapBigSprite(n);
  if(!mc.big) { if(err) strlcpy(err, T("out of memory for the map","sem memória para o mapa","sin memoria para el mapa"), errLen); return false; }
  mc.n = n; mc.z = z; mc.scale = s;
  mc.big->fillScreen(mapBgColor(style));

  const double world = 256.0 * (double)(1UL << z);
  const double latR = lat * M_PI / 180.0;
  const double X0 = (lon + 180.0) / 360.0 * world;
  const double Y0 = (1.0 - log(tan(latR) + 1.0 / cos(latR)) / M_PI) / 2.0 * world;
  mc.worldX = X0 - n / 2.0;
  mc.worldY = Y0 - n / 2.0;
  if(style == MAP_NONE) return true;

  const int tx0 = (int)floor(mc.worldX / 256.0), tx1 = (int)floor((mc.worldX + n - 1) / 256.0);
  const int ty0 = (int)floor(mc.worldY / 256.0), ty1 = (int)floor((mc.worldY + n - 1) / 256.0);
  const int maxT = (1 << z);
  HttpBuf buf;
  int fails = 0;
  for(int ty = ty0; ty <= ty1; ty++)
  {
    if(ty < 0 || ty >= maxT) continue;
    for(int tx = tx0; tx <= tx1; tx++)
    {
      const int wx = ((tx % maxT) + maxT) % maxT;
      const uint32_t key = tileKey(style, z, wx, ty);
      bool ok = cacheGet(key, buf);
      if(!ok)
      {
        char url[200]; tileUrl(url, sizeof(url), style, z, wx, ty);
        char e[48];
        ok = httpGet(url, buf, TILE_MAX, e, sizeof(e), "image/png") == 200 && buf.len > 64;
        if(ok) cachePut(key, buf);
        else { fails++; if(err && errLen) snprintf(err, errLen, "mapa: %s", e); }
      }
      if(ok)
      {
        const int px = (int)lround(tx * 256.0 - mc.worldX), py = (int)lround(ty * 256.0 - mc.worldY);
        mc.big->drawPng(buf.data, buf.len, px, py);
      }
      vTaskDelay(1);
    }
  }
  buf.release();
  return true;
}

static inline uint16_t sw(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }

// Pesos de area por eixo (separavel): cada pixel de saida cobre ate 3 pixels de origem
struct AxisW { int16_t start; uint8_t count; uint8_t w[3]; };

static void axisWeights(AxisW *out, int nOut, int nSrc, double s)
{
  const double off = nSrc / 2.0 - nOut / 2.0 * s;
  for(int i = 0; i < nOut; i++)
  {
    const double a = off + i * s, b = a + s;
    int k0 = (int)floor(a); if(k0 < 0) k0 = 0;
    AxisW &w = out[i]; w.start = k0; w.count = 0;
    int total = 0;
    for(int k = k0; k < nSrc && k < b && w.count < 3; k++)
    {
      const double cov = min((double)k + 1, b) - max((double)k, a);
      if(cov <= 0) continue;
      int v = (int)lround(cov / s * 255.0);
      w.w[w.count++] = (uint8_t)v; total += v;
    }
    if(w.count) w.w[0] += 255 - total;   // soma exata de 255
  }
}

void mapDownsample(const MapCanvas &mc, const uint16_t *src, uint16_t *out)
{
  static AxisW ax[LCD_W], ay[LCD_H];
  const int n = mc.n;
  axisWeights(ax, LCD_W, n, mc.scale);
  axisWeights(ay, LCD_H, n, mc.scale);
  for(int j = 0; j < LCD_H; j++)
  {
    const AxisW &wy = ay[j];
    for(int i = 0; i < LCD_W; i++)
    {
      const AxisW &wx = ax[i];
      uint32_t r = 0, g = 0, b = 0;
      for(int yy = 0; yy < wy.count; yy++)
      {
        const uint16_t *row = src + (wy.start + yy) * n + wx.start;
        uint32_t rr = 0, gg = 0, bb = 0;
        for(int xx = 0; xx < wx.count; xx++)
        {
          const uint16_t v = sw(row[xx]);
          rr += ((v >> 11) & 31) * wx.w[xx]; gg += ((v >> 5) & 63) * wx.w[xx]; bb += (v & 31) * wx.w[xx];
        }
        r += rr * wy.w[yy]; g += gg * wy.w[yy]; b += bb * wy.w[yy];
      }
      const uint16_t o = (uint16_t)((((r + 32512) / 65025) << 11) | (((g + 32512) / 65025) << 5) | ((b + 32512) / 65025));
      out[j * LCD_W + i] = sw(o);
    }
    if((j & 15) == 0) vTaskDelay(1);
  }
}
