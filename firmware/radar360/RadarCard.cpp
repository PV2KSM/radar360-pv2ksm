// Cartao de temperatura sobre o radar de chuva: 11 estilos (feat.wCardStyle).
#include "RadarCard.h"
#include "Gfx.h"
#include "Feat.h"
#include "Sky.h"
#include <math.h>
#include <time.h>
#include <esp_heap_caps.h>

static uint32_t mix(uint32_t a, uint32_t b, float t)
{
  t = constrain(t, 0.0f, 1.0f);
  const int r = ((a >> 16) & 255) + (((int)((b >> 16) & 255) - (int)((a >> 16) & 255)) * t);
  const int g = ((a >> 8) & 255) + (((int)((b >> 8) & 255) - (int)((a >> 8) & 255)) * t);
  const int bl = (a & 255) + (((int)(b & 255) - (int)(a & 255)) * t);
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

// frio azul -> ameno verde-agua -> dourado -> quente coral/vermelho
static uint32_t heat(float t)
{
  if(t < 10) return mix(0x4F7CFFu, 0x3FC8FFu, (t - 0) / 10.0f);
  if(t < 18) return mix(0x3FC8FFu, 0x47E6B5u, (t - 10) / 8.0f);
  if(t < 25) return mix(0x47E6B5u, 0xFFC94Fu, (t - 18) / 7.0f);
  return mix(0xFFC94Fu, 0xFF4A3Du, (t - 25) / 10.0f);
}

static void tempStr(char *b, size_t n, float t) { snprintf(b, n, "%d°", (int)lroundf(t)); }

static const char *windDirName(int deg)
{
  static const char *const N[] = {"N", "NE", "L", "SE", "S", "SO", "O", "NO"};
  return N[((deg % 360 + 360) % 360 + 22) / 45 % 8];
}

static float dewPoint(float t, int hum)
{
  const float g = logf(max(1, hum) / 100.0f) + 17.62f * t / (243.12f + t);
  return 243.12f * g / (17.62f - g);
}

// ---- Desfoque (vidro fosco): caixa deslizante, horizontal e vertical ----
static void blurRect(int x0, int y0, int w, int h, int rad)
{
  x0 = max(0, x0); y0 = max(0, y0); w = min(w, LCD_W - x0); h = min(h, LCD_H - y0);
  if(w <= 0 || h <= 0) return;
  static uint8_t *tmp = nullptr;
  static const int MAXN = 260 * 110;
  if(!tmp) tmp = (uint8_t *)heap_caps_malloc(MAXN * 3, MALLOC_CAP_SPIRAM);
  if(!tmp || w * h > MAXN) return;
  uint16_t *buf = (uint16_t *)canvas.getBuffer();
  for(int j = 0; j < h; j++)
    for(int i = 0; i < w; i++)
    {
      uint16_t v = buf[(y0 + j) * LCD_W + x0 + i]; v = (v >> 8) | (v << 8);
      uint8_t *t = tmp + (j * w + i) * 3;
      t[0] = (v >> 11) << 3; t[1] = ((v >> 5) & 63) << 2; t[2] = (v & 31) << 3;
    }
  const int n = rad * 2 + 1;
  int line[260 * 3];
  for(int pass = 0; pass < 2; pass++)
  {
    for(int j = 0; j < h; j++)   // horizontal
    {
      for(int c = 0; c < 3; c++)
      {
        int s = 0;
        for(int k = -rad; k <= rad; k++) s += tmp[(j * w + constrain(k, 0, w - 1)) * 3 + c];
        for(int i = 0; i < w; i++)
        {
          line[i * 3 + c] = s / n;
          s += tmp[(j * w + min(i + rad + 1, w - 1)) * 3 + c] - tmp[(j * w + max(i - rad, 0)) * 3 + c];
        }
      }
      for(int i = 0; i < w * 3; i++) tmp[j * w * 3 + i] = (uint8_t)line[i];
    }
    for(int i = 0; i < w; i++)   // vertical
    {
      for(int c = 0; c < 3; c++)
      {
        int s = 0;
        for(int k = -rad; k <= rad; k++) s += tmp[(constrain(k, 0, h - 1) * w + i) * 3 + c];
        for(int j = 0; j < h; j++)
        {
          line[j * 3 + c] = s / n;
          s += tmp[(min(j + rad + 1, h - 1) * w + i) * 3 + c] - tmp[(max(j - rad, 0) * w + i) * 3 + c];
        }
      }
      for(int j = 0; j < h; j++) for(int c = 0; c < 3; c++) tmp[(j * w + i) * 3 + c] = (uint8_t)line[j * 3 + c];
    }
  }
  for(int j = 0; j < h; j++)
    for(int i = 0; i < w; i++)
    {
      const uint8_t *t = tmp + (j * w + i) * 3;
      const uint16_t v = ((t[0] >> 3) << 11) | ((t[1] >> 2) << 5) | (t[2] >> 3);
      buf[(y0 + j) * LCD_W + x0 + i] = (v >> 8) | (v << 8);
    }
}

// mascara arredondada: copia o desfoque so dentro do retangulo arredondado
static void glassBox(int x, int y, int w, int h, int r)
{
  static uint16_t *save = nullptr;
  if(!save) save = (uint16_t *)heap_caps_malloc(260 * 110 * 2, MALLOC_CAP_SPIRAM);
  if(save && w <= 260 && h <= 110)
  {
    uint16_t *buf = (uint16_t *)canvas.getBuffer();
    for(int j = 0; j < h; j++) memcpy(save + j * w, buf + (y + j) * LCD_W + x, w * 2);
    blurRect(x, y, w, h, 6);
    // devolve os cantos de fora (sem desfoque)
    for(int j = 0; j < h; j++)
      for(int i = 0; i < w; i++)
      {
        const float hx = w / 2.0f, hy = h / 2.0f;
        const float qx = fabsf(i + 0.5f - hx) - (hx - r), qy = fabsf(j + 0.5f - hy) - (hy - r);
        const float ox = max(qx, 0.0f), oy = max(qy, 0.0f);
        const float d = sqrtf(ox * ox + oy * oy) + min(max(qx, qy), 0.0f) - r;
        if(d > 0.5f) buf[(y + j) * LCD_W + x + i] = save[j * w + i];
      }
  }
  gfxBlendRoundRect(x, y, w, h, r, 0xFFFFFFu, 46, 0xFFFFFFu, 150);
  gfxBlendRoundRect(x + 3, y + 3, w - 6, h / 2 - 3, r - 3, 0xFFFFFFu, 18);   // brilho na metade de cima
  gfxBlendRoundRect(x, y, w, h, r, 0x0E1820u, 40);                          // um pouco de escuro para o texto
}

// ---- Brilho de neon em volta de um retangulo arredondado ----
static void neonBox(int x, int y, int w, int h, int r, uint32_t col, float pulse)
{
  uint16_t *buf = (uint16_t *)canvas.getBuffer();
  const int G = 14;
  for(int j = max(0, y - G); j < min(LCD_H, y + h + G); j++)
    for(int i = max(0, x - G); i < min(LCD_W, x + w + G); i++)
    {
      const float hx = w / 2.0f, hy = h / 2.0f;
      const float qx = fabsf(i + 0.5f - (x + hx)) - (hx - r), qy = fabsf(j + 0.5f - (y + hy)) - (hy - r);
      const float ox = max(qx, 0.0f), oy = max(qy, 0.0f);
      const float d = sqrtf(ox * ox + oy * oy) + min(max(qx, qy), 0.0f) - r;
      uint16_t &p = buf[j * LCD_W + i];
      if(d < -2.2f) { p = gfxBlend(p, 0x05090Cu, 225); continue; }   // miolo escuro
      const float ad = fabsf(d + 0.6f);
      if(ad < 1.6f) { p = gfxBlend(p, mix(col, 0xFFFFFFu, 0.45f), (uint8_t)(255 * constrain(1.6f - ad, 0.0f, 1.0f))); continue; }
      const float g = expf(-ad / (3.2f + 1.6f * pulse));
      if(d < 0) p = gfxBlend(gfxBlend(p, 0x05090Cu, 225), col, (uint8_t)(g * 140));
      else p = gfxBlend(p, col, (uint8_t)(g * (120 + 60 * pulse)));
    }
}

// ---- Digitos de 7 segmentos (LCD) ----
static void seg7(char c, int x, int y, int w, int h, int t, uint32_t on, uint32_t off)
{
  static const uint8_t M[] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
  uint8_t m = 0;
  if(c >= '0' && c <= '9') m = M[c - '0'];
  else if(c == '-') m = 0x40;
  const int hh = h / 2;
  const int sk = 2;   // inclinacao leve
  struct S { int x0, y0, x1, y1; };
  const S seg[7] = {
    {x + t + sk * 2, y, x + w - t + sk * 2, y},                 // a (horizontal)
    {x + w + sk * 2, y + t, x + w + sk, y + hh - t / 2},        // b
    {x + w + sk, y + hh + t / 2, x + w, y + h - t},             // c
    {x + t, y + h, x + w - t, y + h},                           // d
    {x + sk, y + hh + t / 2, x, y + h - t},                     // e
    {x + sk * 2, y + t, x + sk, y + hh - t / 2},                // f
    {x + t + sk, y + hh, x + w - t + sk, y + hh}};              // g
  for(int s = 0; s < 7; s++)
  {
    const uint32_t col = (m >> s) & 1 ? on : off;
    canvas.drawWideLine(seg[s].x0, seg[s].y0, seg[s].x1, seg[s].y1, t / 2.0f, col);
  }
}

// ---- Cada estilo ----
static void glass(const WxNow &n)
{
  char b[16];
  const int x = CX - 104, y = 262, w = 208, h = 64;
  glassBox(x, y, w, h, 22);
  weatherIcon(x + 38, y + 32, 40, n.code, n.day, 0x8899A0u);
  tempStr(b, sizeof(b), n.temp);
  gfxTextShadow(b, x + 72, y + 22, FontTitle, 0xFFFFFFu, middle_left, 0x1A2630);
  gfxTextShadowFit(weatherConditionShort(n.code), x + 72, y + 47, 124, FontSmall, 0xF2F6F8u, middle_left);
}

static void neon(const WxNow &n)
{
  char b[16];
  const uint32_t c = heat(n.temp);
  const float pulse = 0.5f + 0.5f * sinf(millis() / 1000.0f * 1.6f);
  neonBox(CX - 92, 266, 184, 58, 29, c, pulse);
  tempStr(b, sizeof(b), n.temp);
  gfxText(b, CX - 26, 287, FontTitle, mix(c, 0xFFFFFFu, 0.25f), middle_center);
  gfxTextFit(weatherConditionShort(n.code), CX - 26, 310, 104, FxInter13, mix(c, 0xFFFFFFu, 0.55f), middle_center);
  char hl[24]; snprintf(hl, sizeof(hl), "%d°", (int)lroundf(n.tmax));
  gfxText("↑", CX + 44, 284, FxInter13, mix(c, 0xFFFFFFu, 0.4f));
  gfxText(hl, CX + 64, 284, FxOsw14, 0xFFFFFFu);
  snprintf(hl, sizeof(hl), "%d°", (int)lroundf(n.tmin));
  gfxText("↓", CX + 44, 306, FxInter13, mix(c, 0xFFFFFFu, 0.4f));
  gfxText(hl, CX + 64, 306, FxOsw14, 0xFFFFFFu);
}

static void aviation(const WxNow &n)
{
  char l1[40], l2[40];
  const int x = CX - 112, y = 264, w = 224, h = 56;
  gfxBlendRoundRect(x, y, w, h, 5, 0x070C10u, 232, 0x4FD1C5u, 230);
  const time_t now = time(nullptr); struct tm u; gmtime_r(&now, &u);
  const char *id = feat.fIcao[0] ? feat.fIcao : "R360";
  snprintf(l1, sizeof(l1), "%s %02d%02d%02dZ Q%d", id, u.tm_mday, u.tm_hour, u.tm_min, (int)lroundf(n.pressure));
  const bool ifr = n.code == 45 || n.code == 48 || n.code >= 63;
  const bool mvfr = !ifr && (n.code >= 51 || n.cloud >= 90);
  const int kt = (int)lroundf(n.wind / 1.852f);
  const int dir = ((int)lroundf(n.windDir / 10.0f) * 10) % 360;
  const int t = (int)lroundf(n.temp), td = (int)lroundf(dewPoint(n.temp, n.hum));
  char ts[8], tds[8];
  snprintf(ts, sizeof(ts), t < 0 ? "M%02d" : "%02d", abs(t)); snprintf(tds, sizeof(tds), td < 0 ? "M%02d" : "%02d", abs(td));
  if(kt < 2) snprintf(l2, sizeof(l2), "%s/%s 00000KT", ts, tds);
  else snprintf(l2, sizeof(l2), "%s/%s %03d%02dKT", ts, tds, dir == 0 ? 360 : dir, min(kt, 99));
  gfxText(l1, x + 10, y + 16, FxMono14, 0x4FD1C5u, middle_left);
  gfxText(l2, x + 10, y + 39, FxMono20, 0xE6F7F5u, middle_left);
  const char *cat = ifr ? "IFR" : mvfr ? "MVFR" : "VFR";
  const uint32_t cc = ifr ? 0xFF5A5Au : mvfr ? 0x6EA8FFu : 0x9BE15Du;
  const int cw = gfxTextWidth(cat, FxMono14) + 10;
  gfxBlendRoundRect(x + w - cw - 8, y + 30, cw, 18, 3, cc, 60, cc, 220);
  gfxText(cat, x + w - 8 - cw / 2, y + 39, FxMono14, cc);
}

static void minimal(const WxNow &n)
{
  char b[16]; tempStr(b, sizeof(b), n.temp);
  gfxShadeRows(236, 360, 0x000000u, 0, 150);
  gfxTextShadow(b, CX, 276, FxClockL, 0xFFFFFFu, middle_center, 0x000000);
  gfxTextShadowFit(weatherConditionShort(n.code), CX, 322, 200, FontSmall, 0xE8EEF2u, middle_center);
}

static void thermo(const WxNow &n)
{
  char b[24];
  const int x = CX - 98, y = 262, w = 196, h = 64;
  gfxBlendRoundRect(x, y, w, h, 18, 0x0A1210u, 220, 0x35564Fu, 200);
  const uint32_t c = heat(n.temp);
  const int tx = x + 26, top = y + 10, bot = y + 44;
  canvas.fillSmoothRoundRect(tx - 5, top - 2, 10, bot - top + 6, 5, 0x24323Au);
  canvas.fillSmoothCircle(tx, bot + 6, 9, 0x24323Au);
  const float f = constrain((n.temp + 5) / 45.0f, 0.06f, 1.0f);
  const int ly = bot - (int)((bot - top) * f);
  canvas.fillSmoothRoundRect(tx - 3, ly, 6, bot - ly + 4, 3, c);
  canvas.fillSmoothCircle(tx, bot + 6, 7, c);
  for(int k = 0; k <= 4; k++) canvas.drawFastHLine(tx + 7, top + k * (bot - top) / 4, k % 2 ? 3 : 5, 0x6C8C86u);
  tempStr(b, sizeof(b), n.temp);
  gfxText(b, x + 48, y + 24, FontTitle, 0xFFFFFFu, middle_left);
  snprintf(b, sizeof(b), "↑%d°  ↓%d°", (int)lroundf(n.tmax), (int)lroundf(n.tmin));
  gfxText(b, x + 50, y + 49, FxInter13, c, middle_left);
  weatherIcon(x + w - 36, y + 32, 38, n.code, n.day, 0x0A1210u);
}

static void compass(const WxNow &n)
{
  const int cx = CX, cy = 284, R = 44;
  gfxGlow(cx, cy, R + 8, 0x000000u, 120);
  canvas.fillSmoothCircle(cx, cy, R, 0x0A1210u);
  canvas.drawCircle(cx, cy, R, 0x6C8C86u);
  canvas.drawCircle(cx, cy, R - 12, 0x24403Au);
  for(int k = 0; k < 36; k++)
  {
    const float a = k * 10 * DEG_TO_RAD; const int L = k % 9 == 0 ? 6 : 3;
    gfxBlendLine(cx + sinf(a) * (R - 1), cy - cosf(a) * (R - 1), cx + sinf(a) * (R - 1 - L), cy - cosf(a) * (R - 1 - L), 0x9FB6AAu, 200);
  }
  static const char *const L4[] = {"N", "L", "S", "O"};
  for(int k = 0; k < 4; k++)
  {
    const float a = k * 90 * DEG_TO_RAD;
    gfxText(L4[k], cx + sinf(a) * (R - 18), cy - cosf(a) * (R - 18) + 1, FxInter13, k == 0 ? 0xFF7A6Bu : 0x9FB6AAu);
  }
  // seta do vento: entra pela direcao de onde ele vem
  const float a = n.windDir * DEG_TO_RAD;
  const float sx = sinf(a), sy = -cosf(a), px = -sy, py = sx;
  const float tip = R - 14, tail = R + 4;
  const uint32_t ac = 0x4FD1C5u;
  canvas.fillTriangle(cx + sx * tip, cy + sy * tip, cx + sx * tail + px * 7, cy + sy * tail + py * 7, cx + sx * tail - px * 7, cy + sy * tail - py * 7, ac);
  char b[24]; tempStr(b, sizeof(b), n.temp);
  gfxText(b, cx, cy - 2, FontBody, 0xFFFFFFu);
  snprintf(b, sizeof(b), "%s %d km/h", windDirName(n.windDir), (int)lroundf(n.wind));
  const int w = gfxTextWidth(b, FxInter13) + 16;
  gfxBlendRoundRect(cx - w / 2, cy + R + 4, w, 18, 9, 0x0A1210u, 210);
  gfxText(b, cx, cy + R + 13, FxInter13, ac);
}

static void lcdCard(const WxNow &n)
{
  const int x = CX - 96, y = 262, w = 192, h = 64;
  canvas.fillSmoothRoundRect(x, y, w, h, 10, 0x1B1E17u);
  canvas.drawRoundRect(x, y, w, h, 10, 0x4A4F42u);
  canvas.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 9, 0x2C3026u);
  const int sx = x + 8, sy = y + 7, sw = w - 16, sh = h - 14;
  canvas.fillRoundRect(sx, sy, sw, sh, 3, 0xA3B386u);
  for(int j = 0; j < sh; j += 2) canvas.drawFastHLine(sx + 1, sy + j, sw - 2, 0x9DAD80u);
  const uint32_t ON = 0x26301Cu, OFF = 0x96A57Au;
  // temperatura com uma casa
  const int tenths = (int)lroundf(n.temp * 10);
  char d[8]; snprintf(d, sizeof(d), "%3d", abs(tenths) / 10 % 1000);
  const int dw = 18, dh = 30, th = 5, gap = 8;
  int px = sx + 58;
  if(tenths < 0) seg7('-', px - dw - gap, sy + 10, dw, dh, th, ON, OFF);
  for(int i = 1; i < 3; i++) { seg7(d[i] == ' ' ? '!' : d[i], px, sy + 10, dw, dh, th, ON, OFF); px += dw + gap; }
  canvas.fillRect(px - 4, sy + 10 + dh - 2, 4, 4, ON);
  seg7('0' + abs(tenths) % 10, px + 2, sy + 10, dw, dh, th, ON, OFF);
  gfxText("°C", px + dw + 10, sy + 14, FxMono14, ON, middle_left);
  // umidade e simbolo do tempo
  char b[12]; snprintf(b, sizeof(b), "%d%%", n.hum);
  gfxText(b, px + dw + 10, sy + 36, FxMono14, ON, middle_left);
  gfxText("UR", sx + 8, sy + 14, FxMono14, ON, middle_left);
  const bool rain = (n.code >= 51 && n.code <= 67) || n.code >= 80;
  gfxText(rain ? "CHUVA" : n.code >= 3 ? "NUBL" : n.day ? "SOL" : "NOITE", sx + 8, sy + 36, FxMono14, ON, middle_left);
}

static void skyBubble(const WxNow &n)
{
  const int cx = CX, cy = 280, R = 46;
  const uint16_t *src = gfxImageBuf(skySceneName(skyScene()));
  uint16_t *buf = (uint16_t *)canvas.getBuffer();
  gfxGlow(cx, cy, R + 12, 0x000000u, 110);
  const float sc = 300.0f / (2 * R);   // mostra a parte de cima do ceu
  const uint32_t ms = millis();
  const bool rain = (n.code >= 51 && n.code <= 67) || n.code >= 80;
  for(int y = -R - 1; y <= R + 1; y++)
    for(int x = -R - 1; x <= R + 1; x++)
    {
      const float d = sqrtf((float)(x * x + y * y));
      const float cov = constrain(R - d + 0.5f, 0.0f, 1.0f);
      if(cov <= 0) continue;
      // lente: puxa o centro (efeito de bolha)
      const float k = 1.0f - 0.18f * (d / R) * (d / R);
      int u = (int)(CX + x * sc * k), v = (int)(150 + y * sc * k);
      u = constrain(u, 0, LCD_W - 1); v = constrain(v, 0, LCD_H - 1);
      uint16_t &p = buf[(cy + y) * LCD_W + cx + x];
      if(src)
      {
        uint16_t s = src[v * LCD_W + u]; s = (s >> 8) | (s << 8);
        const uint32_t rgb = ((uint32_t)((s >> 11) << 3) << 16) | ((uint32_t)(((s >> 5) & 63) << 2) << 8) | ((s & 31) << 3);
        p = gfxBlend(p, rgb, (uint8_t)(cov * 255));
      }
      else p = gfxBlend(p, n.day ? 0x3F86E2u : 0x0B1433u, (uint8_t)(cov * 255));
      // borda escurecida e reflexo
      const float rim = constrain((d - R * 0.72f) / (R * 0.28f), 0.0f, 1.0f);
      if(rim > 0) p = gfxBlend(p, 0x000814u, (uint8_t)(rim * rim * 120 * cov));
    }
  if(rain)
    for(int i = 0; i < 18; i++)
    {
      const uint32_t h = i * 2654435761u;
      const int x0 = (int)(h % (2 * R)) - R;
      const int yy = (int)((h >> 8) % (2 * R) + ms / 6) % (2 * R) - R;
      if(x0 * x0 + yy * yy > (R - 4) * (R - 4)) continue;
      gfxBlendLine(cx + x0, cy + yy, cx + x0 - 2, cy + yy + 7, 0xCFE3FFu, 150);
    }
  gfxArc(cx, cy, R - 1, R + 1.5f, 0, 360, 0xFFFFFFu, 150);
  gfxArc(cx, cy, R - 7, R - 4, 300, 350, 0xFFFFFFu, 110, 8);   // reflexo do vidro
  char b[16]; tempStr(b, sizeof(b), n.temp);
  gfxTextShadow(b, cx, cy + 16, FontTitle, 0xFFFFFFu, middle_center, 0x000000);
  gfxTextShadowFit(weatherConditionShort(n.code), cx, cy + R + 14, 200, FxInter13, 0xFFFFFFu, middle_center);
}

void radarCardDraw(const WxNow &n)
{
  switch(feat.wCardStyle)
  {
    case 2: neon(n); break;
    case 4: aviation(n); break;
    case 5: minimal(n); break;
    case 6: thermo(n); break;
    case 7: compass(n); break;
    case 9: lcdCard(n); break;
    case 10: skyBubble(n); break;
    default: glass(n); break;   // 0 (classica), 3 (arco) e 8 (etiqueta) foram removidos
  }
}

bool radarCardAnimated() { return feat.wCardStyle == 2 || feat.wCardStyle == 10; }
bool radarCardCoversBottom() { return feat.wCardStyle == 5 || feat.wCardStyle == 7 || feat.wCardStyle == 10; }
