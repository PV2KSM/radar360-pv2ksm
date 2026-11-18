#include "Sky.h"
#include "Gfx.h"
#include "Feat.h"
#include "Weather.h"
#include "Net.h"
#include "Pack.h"
#include <time.h>
#include <math.h>
#include <esp_heap_caps.h>

static const char *const NAMES[SKY_COUNT] = {
  "sky_clear_day", "sky_partly_day", "sky_cloudy_day", "sky_rain_day", "sky_storm", "sky_fog",
  "sky_clear_night", "sky_partly_night", "sky_cloudy_night", "sky_rain_night", "sky_dawn", "sky_dusk"};

const char *skySceneName(uint8_t s) { return s < SKY_COUNT ? NAMES[s] : NAMES[0]; }

static bool isRain(int c) { return (c >= 51 && c <= 67) || (c >= 80 && c <= 82); }

uint8_t skyScene()
{
  const time_t now = time(nullptr);
  struct tm t; localtime_r(&now, &t);
  WxNow n;
  if(!weatherNow(&n))
  {
    const int m = t.tm_hour * 60 + t.tm_min;
    if(abs(m - 6 * 60) < 40) return SKY_DAWN;
    if(abs(m - 18 * 60) < 40) return SKY_DUSK;
    return (m >= 6 * 60 && m < 18 * 60) ? SKY_CLEAR_DAY : SKY_CLEAR_NIGHT;
  }
  const bool sunValid = n.sunrise > 1700000000 && n.sunset > n.sunrise;
  // o nascer/por do sol de "hoje" pode estar um dia atrasado: compara so a hora do dia
  long sr = sunValid ? (long)(n.sunrise % 86400) : 6 * 3600, ss = sunValid ? (long)(n.sunset % 86400) : 18 * 3600;
  const long cur = (long)(now % 86400);
  const bool day = cur >= sr && cur < ss;
  const int c = n.code;
  if(c >= 95) return SKY_STORM;
  if(isRain(c)) return day ? SKY_RAIN_DAY : SKY_RAIN_NIGHT;
  if(c == 45 || c == 48) return SKY_FOG;
  auto near = [](long a, long b) { long d = labs(a - b); if(d > 43200) d = 86400 - d; return d < 45 * 60; };
  if(c <= 2 || c == 3)
  {
    if(near(cur, sr)) return SKY_DAWN;
    if(near(cur, ss)) return SKY_DUSK;
  }
  if(c == 0) return day ? SKY_CLEAR_DAY : SKY_CLEAR_NIGHT;
  if(c <= 2) return day ? SKY_PARTLY_DAY : SKY_PARTLY_NIGHT;
  return day ? SKY_CLOUDY_DAY : SKY_CLOUDY_NIGHT;   // nublado, neve
}

// ---- Fundo sem o pacote visual ----
static void fallback(uint8_t s)
{
  switch(s)
  {
    case SKY_CLEAR_DAY: case SKY_PARTLY_DAY: gfxGradientV(0x1A5CC8u, 0x3F86E2u, 0xBFE0F7u); break;
    case SKY_CLOUDY_DAY: case SKY_FOG: gfxGradientV(0x5C6E84u, 0x8496AAu, 0xC3CDD7u); break;
    case SKY_RAIN_DAY: gfxGradientV(0x3E4A58u, 0x5F6D7Cu, 0x9AA6B1u); break;
    case SKY_STORM: gfxGradientV(0x151A24u, 0x283140u, 0x4A5466u); break;
    case SKY_DAWN: gfxGradientV(0x2B3C78u, 0x9A6E9Cu, 0xFFC18Au); break;
    case SKY_DUSK: gfxGradientV(0x1C2756u, 0x7A4F8Eu, 0xFF9A62u); break;
    default: gfxGradientV(0x03060Fu, 0x0A1430u, 0x1E2E52u); break;
  }
}

// ---- Animacoes ----
static inline float hash(uint32_t i, uint32_t k)
{
  uint32_t x = i * 0x9E3779B1u ^ (k * 0x85EBCA77u);
  x ^= x >> 15; x *= 0x2C1B3C6Du; x ^= x >> 12; x *= 0x297A2D39u; x ^= x >> 15;
  return (x & 0xFFFFFFu) / 16777216.0f;
}

static void rain(uint32_t ms, int count, float slant, uint32_t color, uint8_t alpha)
{
  const float t = ms / 1000.0f;
  for(int i = 0; i < count; i++)
  {
    const float v = 420 + hash(i, 1) * 260;
    const float L = 9 + hash(i, 2) * 16;
    const float y = fmodf(hash(i, 3) * 420 + t * v, 420) - 30;
    const float x = hash(i, 4) * 440 - 40 + y * slant;
    const uint8_t a = alpha * (0.45f + 0.55f * hash(i, 5));
    gfxBlendLine(x, y, x - L * slant, y - L, color, a);
  }
  // respingos no chao
  for(int i = 0; i < count / 6; i++)
  {
    const float ph = fmodf(t * 2.2f + hash(i, 7), 1.0f);
    if(ph > 0.35f) continue;
    const float x = 30 + hash(i, 8) * 300, y = 300 + hash(i, 9) * 50;
    const float r = 2 + ph * 14;
    gfxBlendLine(x - r, y, x + r, y, color, (uint8_t)(alpha * (1 - ph / 0.35f) * 0.6f));
  }
}

static void lightning(uint32_t ms)
{
  const uint32_t cycle = 7300;
  const uint32_t n = ms / cycle, ph = ms % cycle;
  const bool on = ph < 110 || (ph > 190 && ph < 260);
  if(!on || hash(n, 11) < 0.25f) return;   // nem todo ciclo tem raio
  // clarao geral
  gfxBlendRect(0, 0, LCD_W, LCD_H, 0xDDE4FFu, ph < 110 ? 70 : 40);
  // raio ramificado
  float x = 80 + hash(n, 12) * 200, y = 30;
  for(int k = 0; k < 9; k++)
  {
    const float nx = x + (hash(n * 13 + k, 14) - 0.5f) * 46, ny = y + 18 + hash(n * 13 + k, 15) * 18;
    canvas.drawWideLine(x, y, nx, ny, 1.4f, 0xF4F2FFu);
    gfxBlendLine(x - 2, y, nx - 2, ny, 0xA9B8FFu, 120);
    gfxBlendLine(x + 2, y, nx + 2, ny, 0xA9B8FFu, 120);
    if(k == 3 || k == 6)
    {
      const float bx = nx + (hash(n, 20 + k) > 0.5f ? 1 : -1) * (20 + hash(n, 30 + k) * 20), by = ny + 26;
      gfxBlendLine(nx, ny, bx, by, 0xE6E9FFu, 200);
    }
    x = nx; y = ny;
    if(y > 230) break;
  }
}


// ---- Lua: fase do dia e caminho real no ceu (nasce, sobe, se poe) ----
struct MoonPos { float alt, az, dist; };
static MoonPos moonPos(time_t when = 0)
{
  if(!when) when = time(nullptr);
  // Posicao aproximada (precisao de ~1 grau, suficiente para a tela)
  const double d = when / 86400.0 + 2440587.5 - 2451545.0;
  const double R = M_PI / 180;
  const double L = 218.316 + 13.176396 * d, M = 134.963 + 13.064993 * d, F = 93.272 + 13.229350 * d;
  const double lon = (L + 6.289 * sin(M * R)) * R, lat = 5.128 * sin(F * R) * R, e = 23.439 * R;
  const double ra = atan2(sin(lon) * cos(e) - tan(lat) * sin(e), cos(lon));
  const double dec = asin(sin(lat) * cos(e) + cos(lat) * sin(e) * sin(lon));
  const double gmst = fmod(280.46061837 + 360.98564736629 * d, 360.0) * R;
  const double H = gmst + feat.lon * R - ra, phi = feat.lat * R;
  const double alt = asin(sin(phi) * sin(dec) + cos(phi) * cos(dec) * cos(H));
  double az = atan2(-cos(dec) * sin(H), sin(dec) * cos(phi) - cos(dec) * cos(H) * sin(phi));
  if(az < 0) az += 2 * M_PI;
  MoonPos m; m.alt = alt / R; m.az = az / R; m.dist = 385001 - 20905 * cos(M * R);
  return m;
}

float skyMoonAlt(time_t t) { return moonPos(t).alt; }

float skyMoonAge()
{
  const double newMoon = 947182440.0;   // 2000-01-06 18:14 UTC
  double d = (time(nullptr) - newMoon) / 86400.0;
  d = fmod(d, 29.530588853); if(d < 0) d += 29.530588853;
  return (float)d;
}

float skyMoonIllum() { return (1 - cosf(skyMoonAge() / 29.530588853f * 2 * PI)) / 2; }

static void moonDisc(int cx, int cy, int r)
{
  const float age = skyMoonAge();
  const float phi = age / 29.530588853f * 2 * PI;
  const float illum = (1 - cosf(phi)) / 2;
  gfxGlow(cx, cy, r * 4, 0xC9D3F2u, (uint8_t)(25 + illum * 70));
  const bool waxing = phi < PI;
  const bool south = feat.lat < 0;   // no hemisferio sul a lua aparece espelhada
  const float cp = cosf(phi);
  uint16_t *buf = (uint16_t *)canvas.getBuffer();
  for(int y = -r - 1; y <= r + 1; y++)
    for(int x = -r - 1; x <= r + 1; x++)
    {
      const float d = sqrtf((float)(x * x + y * y));
      const float cov = constrain(r - d + 0.5f, 0.0f, 1.0f);
      if(cov <= 0) continue;
      const float ny = (float)y / r, s = sqrtf(max(0.0f, 1 - ny * ny));
      float nx = (float)x / r; if(south) nx = -nx;
      const float e = (waxing ? (nx - cp * s) : (-cp * s - nx)) * r;
      const float lit = constrain(e + 0.5f, 0.0f, 1.0f);
      const int px = cx + x, py = cy + y;
      if((unsigned)px >= LCD_W || (unsigned)py >= LCD_H) continue;
      uint16_t &p = buf[py * LCD_W + px];
      const float mare = (hash((x + 40) / 4 + ((y + 40) / 4) * 31, 77) > 0.72f) ? 0.88f : 1.0f;
      p = gfxBlend(p, 0x1B2234u, (uint8_t)(cov * 200));
      if(lit > 0) p = gfxBlend(p, mare < 1 ? 0xD9D4C4u : 0xF6F2E4u, (uint8_t)(cov * lit * 255));
    }
}

// Onde a lua aparece na tela: olhando para o lado do ceu por onde ela passa
// (norte no hemisferio sul). Some abaixo do horizonte.
static bool moonScreen(int &x, int &y, int &r)
{
  const MoonPos m = moonPos();
  if(m.alt < -1) return false;
  const float facing = feat.lat < 0 ? 0 : 180;
  float rel = m.az - facing; while(rel > 180) rel -= 360; while(rel < -180) rel += 360;
  // leste fica a direita olhando para o norte; a esquerda olhando para o sul
  const float side = feat.lat < 0 ? 1 : -1;
  x = CX + side * sinf(rel * DEG_TO_RAD) * 150;
  y = 286 - sinf(max(0.0f, m.alt) * DEG_TO_RAD) * 236;
  r = (int)lroundf(13 * 385001 / m.dist);   // perto do perigeu fica um pouco maior
  return true;
}

static void moon()
{
  int x, y, r;
  if(moonScreen(x, y, r)) moonDisc(x, y, r);
}

// ---- Nuvens finas passando, sol com brilho vivo, passaros e estrela cadente ----
static void wisps(uint32_t ms, int y0, float speed, uint32_t col, uint8_t alpha)
{
  size_t len = 0;
  const uint8_t *w = packFind("wisps", &len);
  if(!w || len != 512 * 120 / 2) return;
  uint16_t *buf = (uint16_t *)canvas.getBuffer();
  // deslocamento em fracao de pixel: nuvem lenta desliza sem pular
  const float fo = fmodf(ms / 1000.0f * speed, 512.0f);
  const int off = (int)fo;
  const int fr = (int)((fo - off) * 256);
  auto at = [&](int j, int x) { const int i = j * 512 + (x & 511); const uint8_t b = w[i >> 1]; return (i & 1) ? (b & 15) : (b >> 4); };
  for(int j = 0; j < 120; j++)
  {
    const int y = y0 + j; if((unsigned)y >= LCD_H) continue;
    for(int x = 0; x < LCD_W; x++)
    {
      const int v = (at(j, x + off) * (256 - fr) + at(j, x + off + 1) * fr);   // 0..15*256
      if(v < 64) continue;
      uint16_t &p = buf[y * LCD_W + x];
      p = gfxBlend(p, col, (uint8_t)(v * alpha / (15 * 256)));
    }
  }
}

static void sunLife(uint32_t ms, int sx, int sy)
{
  const float t = ms / 1000.0f;
  gfxGlow(sx, sy, 70, 0xFFF4D6u, (uint8_t)(40 + 22 * sinf(t * 0.9f)));
  for(int k = 0; k < 10; k++)
  {
    const float a = (k * 36 + t * 4) * DEG_TO_RAD, L = 70 + 30 * sinf(t * 0.7f + k);
    gfxBlendLine(sx + cosf(a) * 18, sy + sinf(a) * 18, sx + cosf(a) * L, sy + sinf(a) * L, 0xFFF8E6u, 34);
  }
}

static void birds(uint32_t ms, uint32_t col)
{
  const uint32_t cycle = 46000, ph = ms % cycle;
  if(ph > 16000) return;
  const float t = ph / 16000.0f;
  const uint32_t n = ms / cycle;
  const float baseY = 110 + hash(n, 90) * 60;
  for(int b = 0; b < 4; b++)
  {
    const float x = -30 + t * 420 - b * 16 - (b & 1) * 6, y = baseY + b * 7 + sinf(t * 9 + b) * 3;
    const float flap = sinf(ms / 120.0f + b * 1.7f) * 3;
    gfxBlendLine(x - 6, y - 2 - flap, x, y, col, 200);
    gfxBlendLine(x, y, x + 6, y - 2 - flap, col, 200);
  }
}

static void shootingStar(uint32_t ms)
{
  const uint32_t cycle = 23000, ph = ms % cycle;
  if(ph > 700) return;
  const uint32_t n = ms / cycle;
  if(hash(n, 60) < 0.4f) return;
  const float t = ph / 700.0f;
  const float x0 = 60 + hash(n, 61) * 220, y0 = 40 + hash(n, 62) * 60;
  const float x = x0 + t * 110, y = y0 + t * 46;
  for(int k = 0; k < 6; k++) gfxBlendLine(x - (k + 1) * 9, y - (k + 1) * 3.8f, x - k * 9, y - k * 3.8f, 0xFFFFFFu, (uint8_t)(230 * (1 - k / 6.0f) * (1 - t * 0.6f)));
}

static void twinkle(uint32_t ms)
{
  for(int i = 0; i < 14; i++)
  {
    const float x = 20 + hash(i, 40) * 320, y = 20 + hash(i, 41) * 200;
    const float a = 0.5f + 0.5f * sinf(ms / 1000.0f * (1.2f + hash(i, 42) * 2) + hash(i, 43) * 6.28f);
    gfxBlendPixel(x, y, 0xFFFFFFu, (uint8_t)(a * 230));
    gfxBlendPixel(x + 1, y, 0xFFFFFFu, (uint8_t)(a * 90)); gfxBlendPixel(x - 1, y, 0xFFFFFFu, (uint8_t)(a * 90));
    gfxBlendPixel(x, y + 1, 0xFFFFFFu, (uint8_t)(a * 90)); gfxBlendPixel(x, y - 1, 0xFFFFFFu, (uint8_t)(a * 90));
  }
}

// Sombra suave atras dos textos nas cenas claras (nuvens brancas atras de letra branca)
// (mapa de intensidade calculado uma vez; por quadro so mistura)
static const int SY0 = 20, SY1 = 316;
static uint8_t *scrimMap = nullptr;
static void scrim(uint8_t alpha)
{
  if(!scrimMap)
  {
    scrimMap = (uint8_t *)heap_caps_malloc(LCD_W * (SY1 - SY0), MALLOC_CAP_SPIRAM);
    if(!scrimMap) return;
    const float rx = 170, ry = 92, cy = 168;
    for(int y = SY0; y < SY1; y++)
      for(int x = 0; x < LCD_W; x++)
      {
        const float dx = (x - CX) / rx, dy = (y - cy) / ry, d2 = dx * dx + dy * dy;
        scrimMap[(y - SY0) * LCD_W + x] = d2 >= 2.4f ? 0 : (uint8_t)(255 * expf(-d2 * 1.3f));
      }
  }
  uint16_t *buf = (uint16_t *)canvas.getBuffer() + SY0 * LCD_W;
  const uint8_t *m = scrimMap;
  for(int i = 0; i < LCD_W * (SY1 - SY0); i++)
    if(m[i]) buf[i] = gfxBlend(buf[i], 0x0A1220u, (uint8_t)((m[i] * alpha) >> 8));
}

static uint8_t scrimFor(uint8_t s)
{
  switch(s)
  {
    case SKY_CLEAR_DAY: case SKY_PARTLY_DAY: case SKY_CLOUDY_DAY: case SKY_FOG: return 105;
    case SKY_DAWN: case SKY_DUSK: case SKY_RAIN_DAY: return 70;
    default: return 0;
  }
}

// Fundo da cena com a sombra do texto ja aplicada (calculado so quando a cena muda)
static uint16_t *sceneBuf = nullptr;
static int8_t sceneOf = -1;

void skyDraw(bool animate)
{
  const uint8_t s = skyScene();
  if(s != sceneOf || !sceneBuf)
  {
    if(!gfxImage(NAMES[s])) fallback(s);
    if(scrimFor(s)) scrim(scrimFor(s));
    if(!sceneBuf) sceneBuf = (uint16_t *)heap_caps_malloc(LCD_W * LCD_H * 2, MALLOC_CAP_SPIRAM);
    if(sceneBuf) { memcpy(sceneBuf, canvas.getBuffer(), LCD_W * LCD_H * 2); sceneOf = s; }
  }
  else memcpy(canvas.getBuffer(), sceneBuf, LCD_W * LCD_H * 2);
  const uint32_t ms = millis();
  if(animate)
    switch(s)
    {
      case SKY_CLEAR_DAY: sunLife(ms, 268, 78); wisps(ms, 150, 7, 0xFFFFFFu, 70); birds(ms, 0x1E2A36u); break;
      case SKY_PARTLY_DAY: sunLife(ms, 250, 70); wisps(ms, 20, 9, 0xFFFFFFu, 110); wisps(ms + 40000, 150, 5, 0xF4F8FCu, 80); birds(ms, 0x1E2A36u); break;
      case SKY_CLOUDY_DAY: wisps(ms, 10, 8, 0xE2E8EEu, 120); wisps(ms + 30000, 140, 5, 0xC8D0DAu, 90); break;
      case SKY_FOG: wisps(ms, 160, 6, 0xF0F3F5u, 150); wisps(ms + 50000, 220, 4, 0xE6EBEFu, 140); break;
      case SKY_RAIN_DAY: wisps(ms, 0, 10, 0xB0BAC6u, 110); break;
      case SKY_RAIN_NIGHT: case SKY_CLOUDY_NIGHT: wisps(ms, 0, 8, 0x3C4760u, 120); break;
      case SKY_STORM: wisps(ms, 0, 14, 0x4A5468u, 130); break;
      case SKY_DAWN: wisps(ms, 40, 6, 0xFFC4A8u, 90); sunLife(ms, 120, 262); birds(ms, 0x2A1E30u); break;
      case SKY_DUSK: wisps(ms, 40, 6, 0xFFB090u, 90); sunLife(ms, 240, 262); birds(ms, 0x22162Au); break;
      case SKY_PARTLY_NIGHT: wisps(ms, 30, 6, 0x4A5878u, 90); break;
      default: break;
    }
  switch(s)
  {
    case SKY_CLEAR_NIGHT: moon(); if(animate) { twinkle(ms); shootingStar(ms); } break;
    case SKY_PARTLY_NIGHT: moon(); if(animate) twinkle(ms); break;
    case SKY_RAIN_DAY: if(animate) rain(ms, 110, 0.16f, 0xE4ECF6u, 120); break;
    case SKY_RAIN_NIGHT: if(animate) rain(ms, 110, 0.16f, 0xB8C8E6u, 110); break;
    case SKY_STORM: if(animate) { rain(ms, 150, 0.32f, 0xD0DAEEu, 130); lightning(ms); } break;
    default: break;
  }
}

uint32_t skyFrameMs()
{
  switch(skyScene())
  {
    case SKY_RAIN_DAY: case SKY_RAIN_NIGHT: case SKY_STORM: return 40;
    default: return 40;               // nuvens passando, sol, passaros, estrelas
  }
}
