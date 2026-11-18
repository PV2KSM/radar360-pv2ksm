// Tela da Lua: fase com a textura de verdade (mares e crateras), % iluminada,
// nascer e por da lua no local configurado, proximas cheia e nova.
#include "Moon.h"
#include "Lang.h"
#include "Gfx.h"
#include "Feat.h"
#include "Sky.h"
#include "Pack.h"
#include <math.h>
#include <time.h>

static const float SYNODIC = 29.530588853f;
static const int TEX = 132;   // tools/moon_tex.py (8 bits de cinza)

const char *moonPhaseName(float age)
{
  const float f = age / SYNODIC;
  if(f < 0.0169f || f >= 0.9831f) return T("New moon","Lua nova","Luna nueva");
  if(f < 0.2331f) return T("Waxing crescent","Lua crescente","Luna creciente");
  if(f < 0.2669f) return T("First quarter","Quarto crescente","Cuarto creciente");
  if(f < 0.4831f) return T("Waxing gibbous","Crescente gibosa","Gibosa creciente");
  if(f < 0.5169f) return T("Full moon","Lua cheia","Luna llena");
  if(f < 0.7331f) return T("Waning gibbous","Minguante gibosa","Gibosa menguante");
  if(f < 0.7669f) return T("Last quarter","Quarto minguante","Cuarto menguante");
  return T("Waning crescent","Lua minguante","Luna menguante");
}

// ---- Nascer e por (procura a passagem pelo horizonte ao longo do dia local) ----
static time_t riseT = 0, setT = 0;
static int cacheDay = -1;
static double cacheLat = 999, cacheLon = 999;

static void computeRiseSet()
{
  const time_t now = time(nullptr);
  struct tm t; localtime_r(&now, &t);
  if(t.tm_yday == cacheDay && cacheLat == feat.lat && cacheLon == feat.lon) return;
  cacheDay = t.tm_yday; cacheLat = feat.lat; cacheLon = feat.lon;
  t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0;
  const time_t t0 = mktime(&t);
  const float H = 0.13f;   // horizonte da lua (paralaxe - refracao - raio)
  riseT = setT = 0;
  float prev = skyMoonAlt(t0) - H;
  for(int k = 1; k <= 144; k++)
  {
    const time_t tk = t0 + k * 600;
    const float a = skyMoonAlt(tk) - H;
    if((prev < 0) != (a < 0))
    {
      const time_t at = tk - 600 + (time_t)(600 * prev / (prev - a));
      if(a >= 0 && !riseT) riseT = at;
      if(a < 0 && !setT) setT = at;
    }
    prev = a;
  }
}

// ---- Disco com textura, terminador suave e luz cinzenta no lado escuro ----
static void moonDiscTex(int cx, int cy, int r, float age)
{
  size_t len = 0;
  const uint8_t *tex = packFind("moon_tex", &len);
  if(len != (size_t)TEX * TEX) tex = nullptr;
  const float phi = age / SYNODIC * 2 * PI;
  const bool south = feat.lat < 0;
  // luz vinda da direita na lua crescente (vista do norte); no sul tudo gira 180 graus
  float lx = sinf(phi), lz = -cosf(phi);
  if(south) lx = -lx;
  uint16_t *buf = (uint16_t *)canvas.getBuffer();
  const float k = (TEX / 2.0f) / r;
  for(int y = -r - 1; y <= r + 1; y++)
  {
    const int py = cy + y; if((unsigned)py >= LCD_H) continue;
    for(int x = -r - 1; x <= r + 1; x++)
    {
      const int px = cx + x; if((unsigned)px >= LCD_W) continue;
      const float d = sqrtf((float)(x * x + y * y));
      const float cov = constrain(r - d + 0.5f, 0.0f, 1.0f);
      if(cov <= 0) continue;
      const float nx = x / (float)r, ny = y / (float)r;
      const float nz = sqrtf(max(0.0f, 1 - nx * nx - ny * ny));
      // albedo (bilinear); no hemisferio sul a imagem fica de cabeca para baixo
      float alb = 0.78f;
      if(tex)
      {
        float tx = (south ? -x : x) * k + TEX / 2.0f - 0.5f, ty = (south ? -y : y) * k + TEX / 2.0f - 0.5f;
        tx = constrain(tx, 0.0f, TEX - 1.001f); ty = constrain(ty, 0.0f, TEX - 1.001f);
        const int ix = (int)tx, iy = (int)ty; const float fx = tx - ix, fy = ty - iy;
        const uint8_t *p = tex + iy * TEX + ix;
        const float a0 = p[0] + (p[1] - p[0]) * fx, a1 = p[TEX] + (p[TEX + 1] - p[TEX]) * fx;
        alb = (a0 + (a1 - a0) * fy) / 255.0f;
      }
      // Lommel-Seeliger: a lua cheia parece "chapada", como a de verdade
      const float mu0 = nx * lx + nz * lz, mu = max(0.05f, nz);
      float lit = mu0 > 0 ? 2 * mu0 / (mu0 + mu) : 0;
      lit = min(1.0f, lit) * constrain((mu0 + 0.03f) / 0.09f, 0.0f, 1.0f);
      const float earth = 0.07f;   // luz cinzenta
      const float v = alb * lit;
      const int R = (int)min(255.0f, v * 250 + alb * earth * 110 + 6);
      const int G = (int)min(255.0f, v * 246 + alb * earth * 125 + 8);
      const int B = (int)min(255.0f, v * 232 + alb * earth * 160 + 18);
      uint16_t &q = buf[py * LCD_W + px];
      q = gfxBlend(q, ((uint32_t)R << 16) | (G << 8) | B, (uint8_t)(cov * 255));
    }
  }
}

static uint32_t hash32(uint32_t x) { x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16; return x; }

static void stars(uint32_t ms)
{
  for(int i = 0; i < 90; i++)
  {
    const uint32_t h = hash32(i * 2654435761u + 17);
    const int x = 10 + h % 340, y = 10 + (h >> 9) % 340;
    const int dx = x - CX, dy = y - CY; if(dx * dx + dy * dy > 172 * 172) continue;
    const float tw = 0.7f + 0.3f * sinf(ms / 1000.0f * (0.8f + (h >> 20) % 7 * 0.25f) + (h & 63));
    const uint8_t a = (uint8_t)((130 + (h >> 24) % 126) * tw);
    gfxBlendPixel(x, y, (h & 1) ? 0xDDE6FFu : 0xFFF2DCu, a);
    if((h >> 28) < 2) { gfxBlendPixel(x + 1, y, 0xDDE6FFu, a / 3); gfxBlendPixel(x - 1, y, 0xDDE6FFu, a / 3); gfxBlendPixel(x, y + 1, 0xDDE6FFu, a / 3); gfxBlendPixel(x, y - 1, 0xDDE6FFu, a / 3); }
  }
}

static void hm(char *b, size_t n, time_t t)
{
  if(!t) { strlcpy(b, "--:--", n); return; }
  struct tm tm; localtime_r(&t, &tm); snprintf(b, n, "%02d:%02d", tm.tm_hour, tm.tm_min);
}

static void dm(char *b, size_t n, float days)
{
  const time_t t = time(nullptr) + (time_t)(days * 86400);
  struct tm tm; localtime_r(&t, &tm); snprintf(b, n, "%02d/%02d", tm.tm_mday, tm.tm_mon + 1);
}

void moonDraw()
{
  const uint32_t ms = millis();
  gfxGradientV(0x0B1433u, 0x070C20u, 0x03050Fu);
  stars(ms);
  const float age = skyMoonAge();
  const float illum = skyMoonIllum();
  const uint32_t GOLD = 0xE8C77Au, SOFT = 0xA9B3D6u;

  // anel do ciclo: nova no topo, cheia embaixo
  const float prog = age / SYNODIC * 360.0f;
  gfxArc(CX, CY, 170, 173, 0, 360, 0x253056u, 255);
  if(prog > 0.5f) gfxArc(CX, CY, 169, 174, 0, prog, GOLD, 230, 1.0f);
  for(int q = 0; q < 4; q++)
  {
    const float a = q * 90 * DEG_TO_RAD;
    canvas.fillSmoothCircle(CX + sinf(a) * 171.5f, CY - cosf(a) * 171.5f, q == 2 ? 4 : 3, q * 90 <= prog ? GOLD : 0x3A4672u);
  }
  const float pa = prog * DEG_TO_RAD;
  gfxGlow(CX + sinf(pa) * 171.5f, CY - cosf(pa) * 171.5f, 12, GOLD, 120);
  canvas.fillSmoothCircle(CX + sinf(pa) * 171.5f, CY - cosf(pa) * 171.5f, 5, 0xFFF3D0u);

  gfxTextSpaced(T("MOON","LUA","LUNA"), CX, 40, FxCinzel14, SOFT, 6);
  const int mx = CX, my = 130, mr = 64;
  gfxGlow(mx, my, mr * 2 + 20, 0xC9D3F2u, (uint8_t)(18 + illum * 60));
  moonDiscTex(mx, my, mr, age);

  gfxText(moonPhaseName(age), CX, 224, FxSerif30, 0xFFFFFFu);
  char b[64], a1[8], a2[8];
  snprintf(b, sizeof(b), T("%d%% lit · day %d of cycle","%d%% iluminada · dia %d do ciclo","%d%% iluminada · día %d del ciclo"), (int)lroundf(illum * 100), (int)age + 1);
  gfxText(b, CX, 254, FontSmall, SOFT);

  computeRiseSet();
  hm(a1, sizeof(a1), riseT); hm(a2, sizeof(a2), setT);
  const int colL = CX - 62, colR = CX + 62;
  gfxText(T("RISE","NASCE","SALE"), colL, 280, FxOsw14, GOLD);
  gfxText(a1, colL, 300, FxOsw18, 0xFFFFFFu);
  gfxText(T("SET","PÕE","SE PONE"), colR, 280, FxOsw14, GOLD);
  gfxText(a2, colR, 300, FxOsw18, 0xFFFFFFu);
  canvas.drawFastVLine(CX, 274, 34, 0x3A4672u);

  float toFull = 0.5f * SYNODIC - age; if(toFull < -0.5f) toFull += SYNODIC;
  float toNew = SYNODIC - age; if(toNew > SYNODIC - 0.5f) toNew -= SYNODIC;
  char f[8], nw[8];
  if(fabsf(toFull) <= 0.5f) strlcpy(f, "hoje", sizeof(f)); else dm(f, sizeof(f), toFull);
  if(fabsf(toNew) <= 0.5f) strlcpy(nw, "hoje", sizeof(nw)); else dm(nw, sizeof(nw), toNew);
  if(toFull < toNew) snprintf(b, sizeof(b), T("Full %s · New %s","Cheia %s · Nova %s","Llena %s · Nueva %s"), f, nw);
  else snprintf(b, sizeof(b), T("New %s · Full %s","Nova %s · Cheia %s","Nueva %s · Llena %s"), nw, f);
  gfxTextFit(b, CX, 328, 200, FxInter13, SOFT);
}

uint32_t moonFrameMs() { return 160; }
