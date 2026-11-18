#include "Gfx.h"
#include "Lang.h"
#include <time.h>

static const uint8_t *loaded = nullptr;

void gfxFont(const uint8_t *font)
{
  if(font == loaded) return;
  canvas.unloadFont();
  canvas.loadFont(font);
  loaded = font;
}

void gfxText(const char *s, int x, int y, const uint8_t *font, uint32_t color, textdatum_t datum)
{
  gfxFont(font);
  canvas.setTextDatum(datum);
  canvas.setTextColor(color);
  canvas.drawString(s, x, y);
}

int gfxTextWidth(const char *s, const uint8_t *font)
{
  gfxFont(font);
  return canvas.textWidth(s);
}

static void utf8Trim(char *buf, size_t &n)
{
  if(n) buf[--n] = 0;
  while(n && (buf[n - 1] & 0xC0) == 0x80) buf[--n] = 0;
  if(n && (unsigned char)buf[n - 1] >= 0xC0) buf[--n] = 0;
}

void gfxTextFit(const char *s, int x, int y, int maxW, const uint8_t *font, uint32_t color, textdatum_t datum)
{
  gfxFont(font);
  canvas.setTextDatum(datum);
  canvas.setTextColor(color);
  if(canvas.textWidth(s) <= maxW) { canvas.drawString(s, x, y); return; }
  char buf[160]; strlcpy(buf, s, sizeof(buf));
  size_t n = strlen(buf);
  while(n > 1)
  {
    utf8Trim(buf, n);
    char tmp[170]; snprintf(tmp, sizeof(tmp), "%s…", buf);
    if(canvas.textWidth(tmp) <= maxW) { canvas.drawString(tmp, x, y); return; }
  }
}

int gfxRoundHalfWidth(int y, int margin)
{
  const float r = LCD_W / 2.0f - margin;
  const float dy = y - CY;
  if(fabsf(dy) >= r) return 0;
  return (int)sqrtf(r * r - dy * dy);
}

int gfxWrapRound(const char *s, int yTop, const uint8_t *font, uint32_t color, int lineH, int maxLines, int margin, bool draw)
{
  gfxFont(font);
  canvas.setTextDatum(middle_center);
  canvas.setTextColor(color);
  int lines = 0, y = yTop + lineH / 2;
  const char *p = s;
  while(*p && lines < maxLines)
  {
    while(*p == ' ') p++;
    // Largura util: a menor entre o topo e a base da linha
    const int w = 2 * min(gfxRoundHalfWidth(y - lineH / 2, margin), gfxRoundHalfWidth(y + lineH / 2, margin));
    char line[200]; size_t len = 0; const char *lastSpace = nullptr; size_t lenAtSpace = 0;
    const char *q = p;
    while(*q)
    {
      // proximo caractere UTF-8
      size_t cl = 1; unsigned char c = *q; if(c >= 0xF0) cl = 4; else if(c >= 0xE0) cl = 3; else if(c >= 0xC0) cl = 2;
      if(len + cl >= sizeof(line) - 4) break;
      memcpy(line + len, q, cl); line[len + cl] = 0;
      if(canvas.textWidth(line) > w) break;
      if(*q == ' ') { lastSpace = q; lenAtSpace = len; }
      len += cl; q += cl;
    }
    line[len] = 0;
    if(*q && lastSpace && lines < maxLines - 1) { line[lenAtSpace] = 0; q = lastSpace + 1; }
    else if(*q && lines == maxLines - 1)
    {
      // ultima linha: reticencias
      while(len > 0)
      {
        char tmp[210]; snprintf(tmp, sizeof(tmp), "%s…", line);
        if(canvas.textWidth(tmp) <= w) { strlcpy(line, tmp, sizeof(line)); break; }
        utf8Trim(line, len);
      }
    }
    if(draw) canvas.drawString(line, CX, y);
    lines++; y += lineH; p = q;
    if(len == 0) break;
  }
  return lines * lineH;
}

int gfxWrapRoundCentered(const char *s, int cy, const uint8_t *font, uint32_t color, int lineH, int maxLines, int margin, bool draw, int *needed)
{
  // mede onde o bloco vai ficar (a largura do circulo muda com a altura)
  int h = gfxWrapRound(s, cy - lineH, font, color, lineH, maxLines, margin, false);
  for(int k = 0; k < 2; k++) h = gfxWrapRound(s, cy - h / 2, font, color, lineH, maxLines, margin, false);
  if(needed) *needed = gfxWrapRound(s, cy - h / 2, font, color, lineH, 99, margin, false) / lineH;
  if(draw) gfxWrapRound(s, cy - h / 2, font, color, lineH, maxLines, margin, true);
  return h / lineH;
}

static inline uint16_t swap16(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }

uint16_t gfxBlend(uint16_t bgSwapped, uint32_t rgb, uint8_t a)
{
  const uint16_t b = swap16(bgSwapped);
  int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  const int fr = (rgb >> 19) & 31, fg = (rgb >> 10) & 63, fb = (rgb >> 3) & 31;
  br += ((fr - br) * a) >> 8; bg += ((fg - bg) * a) >> 8; bb += ((fb - bb) * a) >> 8;
  return swap16((uint16_t)((br << 11) | (bg << 5) | bb));
}

void gfxBlendPixel(int x, int y, uint32_t rgb, uint8_t a)
{
  if((unsigned)x >= LCD_W || (unsigned)y >= LCD_H || !a) return;
  uint16_t *buf = (uint16_t *)canvas.getBuffer();
  uint16_t &p = buf[y * LCD_W + x];
  p = gfxBlend(p, rgb, a);
}

// Linha de Xiaolin Wu com alfa global
void gfxBlendLine(float x0, float y0, float x1, float y1, uint32_t rgb, uint8_t alpha)
{
  const bool steep = fabsf(y1 - y0) > fabsf(x1 - x0);
  if(steep) { std::swap(x0, y0); std::swap(x1, y1); }
  if(x0 > x1) { std::swap(x0, x1); std::swap(y0, y1); }
  const float dx = x1 - x0, dy = y1 - y0, g = dx == 0 ? 1 : dy / dx;
  float y = y0;
  for(int x = (int)roundf(x0); x <= (int)roundf(x1); x++)
  {
    const int yi = (int)floorf(y);
    const float f = y - yi;
    const uint8_t a1 = (uint8_t)(alpha * (1 - f)), a2 = (uint8_t)(alpha * f);
    if(steep) { gfxBlendPixel(yi, x, rgb, a1); gfxBlendPixel(yi + 1, x, rgb, a2); }
    else { gfxBlendPixel(x, yi, rgb, a1); gfxBlendPixel(x, yi + 1, rgb, a2); }
    y += g;
  }
}

void gfxDim(uint8_t keep)
{
  uint16_t *buf = (uint16_t *)canvas.getBuffer();
  for(int i = 0; i < LCD_W * LCD_H; i++)
  {
    const uint16_t v = swap16(buf[i]);
    const int r = (((v >> 11) & 31) * keep) >> 8, g = (((v >> 5) & 63) * keep) >> 8, b = ((v & 31) * keep) >> 8;
    buf[i] = swap16((uint16_t)((r << 11) | (g << 5) | b));
  }
}

void gfxBackground()
{
  canvas.fillScreen(C_BG);
  canvas.drawCircle(CX, CY, 178, C_RING);
}

void gfxWifiBars(int x, int y, int rssi, uint32_t on, uint32_t off)
{
  const int level = rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : rssi >= -85 ? 1 : 0;
  for(int i = 0; i < 4; i++)
  {
    const int h = 5 + i * 4;
    canvas.fillSmoothRoundRect(x + i * 7, y - h, 5, h, 1, i < level ? on : off);
  }
}

void gfxStatusLine(const char *msg, uint32_t color)
{
  gfxWrapRoundCentered(msg, CY, FontBody, color, 30, 3, 40);
}

const char *gfxAgo(time_t t, char *buf, size_t n)
{
  const long d = (long)(time(nullptr) - t);
  if(t <= 0) snprintf(buf, n, "-");
  else if(d < 60) snprintf(buf, n, "agora");
  else if(d < 3600) snprintf(buf, n, T("%ld min ago","há %ld min","hace %ld min"), d / 60);
  else if(d < 86400) snprintf(buf, n, T("%ld h ago","há %ld h","hace %ld h"), d / 3600);
  else snprintf(buf, n, T("%ld d ago","há %ld d","hace %ld d"), d / 86400);
  return buf;
}

// ================= Temas =================
#include "Pack.h"
#include <esp_heap_caps.h>

void gfxTextShadow(const char *s, int x, int y, const uint8_t *font, uint32_t color, textdatum_t datum, uint32_t shadow)
{
  gfxFont(font);
  canvas.setTextDatum(datum);
  canvas.setTextColor(shadow);
  canvas.drawString(s, x + 1, y + 2);
  canvas.setTextColor(color);
  canvas.drawString(s, x, y);
}

void gfxTextShadowFit(const char *s, int x, int y, int maxW, const uint8_t *font, uint32_t color, textdatum_t datum)
{
  gfxFont(font);
  char buf[170]; strlcpy(buf, s, sizeof(buf));
  size_t n = strlen(buf);
  if(canvas.textWidth(buf) > maxW)
    while(n > 1)
    {
      utf8Trim(buf, n);
      char tmp[180]; snprintf(tmp, sizeof(tmp), "%s…", buf);
      if(canvas.textWidth(tmp) <= maxW) { strlcpy(buf, tmp, sizeof(buf)); break; }
    }
  gfxTextShadow(buf, x, y, font, color, datum);
}

void gfxTextSpaced(const char *s, int x, int y, const uint8_t *font, uint32_t color, int gap, textdatum_t datum)
{
  gfxFont(font);
  // largura total
  char ch[5]; int total = 0, n = 0;
  for(const char *p = s; *p;)
  {
    size_t cl = 1; unsigned char c = *p; if(c >= 0xF0) cl = 4; else if(c >= 0xE0) cl = 3; else if(c >= 0xC0) cl = 2;
    memcpy(ch, p, cl); ch[cl] = 0; total += canvas.textWidth(ch); n++; p += cl;
  }
  total += gap * max(0, n - 1);
  int cx = datum == middle_left ? x : datum == middle_right ? x - total : x - total / 2;
  canvas.setTextDatum(middle_left);
  canvas.setTextColor(color);
  for(const char *p = s; *p;)
  {
    size_t cl = 1; unsigned char c = *p; if(c >= 0xF0) cl = 4; else if(c >= 0xE0) cl = 3; else if(c >= 0xC0) cl = 2;
    memcpy(ch, p, cl); ch[cl] = 0;
    canvas.drawString(ch, cx, y);
    cx += canvas.textWidth(ch) + gap; p += cl;
  }
}

static inline int nibA(const uint8_t *p, int i) { const uint8_t b = p[i >> 1]; return ((i & 1) ? (b & 15) : (b >> 4)) * 17; }

void gfxMask(const uint8_t *a, int w, int h, int x, int y, uint32_t color, uint8_t alpha)
{
  if(!a) return;
  uint16_t *buf = (uint16_t *)canvas.getBuffer();
  for(int j = 0; j < h; j++)
  {
    const int yy = y + j; if((unsigned)yy >= LCD_H) continue;
    for(int i = 0; i < w; i++)
    {
      const int xx = x + i; if((unsigned)xx >= LCD_W) continue;
      const uint8_t v = nibA(a, j * w + i); if(!v) continue;
      uint16_t &p = buf[yy * LCD_W + xx];
      p = gfxBlend(p, color, alpha == 255 ? v : (v * alpha) >> 8);
    }
  }
}

void gfxMaskScaled(const uint8_t *a, int w, int h, int cx, int cy, int outW, uint32_t color, uint8_t alpha)
{
  if(!a) return;
  const int outH = outW * h / w;
  uint16_t *buf = (uint16_t *)canvas.getBuffer();
  const int x0 = cx - outW / 2, y0 = cy - outH / 2;
  for(int j = 0; j < outH; j++)
  {
    const int yy = y0 + j; if((unsigned)yy >= LCD_H) continue;
    const float sy = (j + 0.5f) * h / outH - 0.5f;
    const int iy = max(0, min(h - 2, (int)sy)); const float fy = constrain(sy - iy, 0.0f, 1.0f);
    for(int i = 0; i < outW; i++)
    {
      const int xx = x0 + i; if((unsigned)xx >= LCD_W) continue;
      const float sx = (i + 0.5f) * w / outW - 0.5f;
      const int ix = max(0, min(w - 2, (int)sx)); const float fx = constrain(sx - ix, 0.0f, 1.0f);
      const float v = nibA(a, iy * w + ix) * (1 - fx) * (1 - fy) + nibA(a, iy * w + ix + 1) * fx * (1 - fy) +
                      nibA(a, (iy + 1) * w + ix) * (1 - fx) * fy + nibA(a, (iy + 1) * w + ix + 1) * fx * fy;
      if(v < 1) continue;
      uint16_t &p = buf[yy * LCD_W + xx];
      p = gfxBlend(p, color, (uint8_t)(v * alpha / 255));
    }
  }
}

void gfxMaskRotated(const uint8_t *a, int n, int cx, int cy, float deg, uint32_t color, uint8_t alpha)
{
  if(!a) return;
  uint16_t *buf = (uint16_t *)canvas.getBuffer();
  const float r = deg * DEG_TO_RAD, c = cosf(r), s = sinf(r), h = n / 2.0f;
  const int R = (int)(h * 1.0f) + 1;
  for(int y = max(0, cy - R); y < min(LCD_H, cy + R); y++)
    for(int x = max(0, cx - R); x < min(LCD_W, cx + R); x++)
    {
      const float dx = x - cx + 0.5f, dy = y - cy + 0.5f;
      const float sx = dx * c + dy * s + h - 0.5f, sy = -dx * s + dy * c + h - 0.5f;
      const int ix = (int)floorf(sx), iy = (int)floorf(sy);
      if(ix < 0 || iy < 0 || ix >= n - 1 || iy >= n - 1) continue;
      const float fx = sx - ix, fy = sy - iy;
      const float v = nibA(a, iy * n + ix) * (1 - fx) * (1 - fy) + nibA(a, iy * n + ix + 1) * fx * (1 - fy) +
                      nibA(a, (iy + 1) * n + ix) * (1 - fx) * fy + nibA(a, (iy + 1) * n + ix + 1) * fx * fy;
      if(v < 1) continue;
      uint16_t &p = buf[y * LCD_W + x];
      p = gfxBlend(p, color, (uint8_t)(v * alpha / 255));
    }
}

void gfxStar(float cx, float cy, float r, uint32_t color)
{
  float px[10], py[10];
  for(int i = 0; i < 10; i++)
  {
    const float a = (i * 36 - 90) * DEG_TO_RAD, rr = (i & 1) ? r * 0.42f : r;
    px[i] = cx + cosf(a) * rr; py[i] = cy + sinf(a) * rr;
  }
  for(int i = 0; i < 10; i++) canvas.fillTriangle(cx, cy, px[i], py[i], px[(i + 1) % 10], py[(i + 1) % 10], color);
  for(int i = 0; i < 10; i++) gfxBlendLine(px[i], py[i], px[(i + 1) % 10], py[(i + 1) % 10], color, 160);
}

void gfxArc(float cx, float cy, float rIn, float rOut, float a0, float a1, uint32_t color, uint8_t alpha, float feather)
{
  uint16_t *buf = (uint16_t *)canvas.getBuffer();
  const bool full = a1 - a0 >= 359.99f;
  while(a0 < 0) { a0 += 360; a1 += 360; }
  // caixa que envolve so o trecho do arco (anel inteiro: o quadrado todo)
  float bx0 = cx - rOut - 1, bx1 = cx + rOut + 1, by0 = cy - rOut - 1, by1 = cy + rOut + 1;
  if(!full)
  {
    bx0 = by0 = 1e9f; bx1 = by1 = -1e9f;
    const int steps = max(2, (int)((a1 - a0) / 5) + 1);
    for(int k = 0; k <= steps; k++)
    {
      const float a = (a0 + (a1 - a0) * k / steps) * DEG_TO_RAD;
      for(float r : {rIn, rOut})
      {
        const float x = cx + sinf(a) * r, y = cy - cosf(a) * r;
        bx0 = min(bx0, x); bx1 = max(bx1, x); by0 = min(by0, y); by1 = max(by1, y);
      }
    }
    bx0 -= 3; by0 -= 3; bx1 += 3; by1 += 3;
  }
  const int x0 = max(0, (int)bx0), x1 = min(LCD_W - 1, (int)bx1);
  const int y0 = max(0, (int)by0), y1 = min(LCD_H - 1, (int)by1);
  const float rIn2 = (rIn - 1) * (rIn - 1), rOut2 = (rOut + 1) * (rOut + 1);
  for(int y = y0; y <= y1; y++)
  {
    const float dy = y - cy, dy2 = dy * dy;
    if(dy2 > rOut2) continue;
    // so os dois trechos da linha que cruzam o anel (antes percorria o quadrado inteiro, inclusive o furo)
    const float xo = sqrtf(rOut2 - dy2) + 1;
    const float xi = dy2 < rIn2 ? sqrtf(rIn2 - dy2) - 1 : -1;
    const int spans[2][2] = {{(int)floorf(cx - xo), xi > 0 ? (int)ceilf(cx - xi) : (int)ceilf(cx + xo)},
                             {xi > 0 ? (int)floorf(cx + xi) : 1 << 20, (int)ceilf(cx + xo)}};
    for(const auto &sp : spans)
    for(int x = max(x0, sp[0]); x <= min(x1, sp[1]); x++)
    {
      const float dx = x - cx, d2 = dx * dx + dy * dy;
      if(d2 > rOut2 || d2 < rIn2) continue;
      const float d = sqrtf(d2);
      float cov = min(1.0f, rOut - d + 0.5f) * min(1.0f, d - rIn + 0.5f);
      if(cov <= 0) continue;
      if(!full)
      {
        float ang = atan2f(dx, -dy) * RAD_TO_DEG; if(ang < 0) ang += 360;
        if(ang < a0) ang += 360;
        if(ang > a1)
        {
          if(feather > 0) continue;
          // fora do arco: suaviza so a borda (1 px)
          float e1 = (ang - a1) * DEG_TO_RAD * d, e0 = (a0 + 360 - ang) * DEG_TO_RAD * d;
          const float e = min(e1, e0);
          if(e >= 1) continue;
          cov *= 1 - e;
        }
        else
        {
          const float e = min((ang - a0), (a1 - ang)) * DEG_TO_RAD * d;
          if(e < 1) cov *= 0.5f + e * 0.5f;
          if(feather > 0) { const float f = min(ang - a0, a1 - ang) / feather; if(f < 1) cov *= f * f * (3 - 2 * f); }
        }
      }
      uint16_t &p = buf[y * LCD_W + x];
      p = gfxBlend(p, color, (uint8_t)(cov * alpha));
    }
  }
}

void gfxGlow(int cx, int cy, int r, uint32_t color, uint8_t alpha)
{
  uint16_t *buf = (uint16_t *)canvas.getBuffer();
  for(int y = max(0, cy - r); y <= min(LCD_H - 1, cy + r); y++)
    for(int x = max(0, cx - r); x <= min(LCD_W - 1, cx + r); x++)
    {
      const float d = sqrtf((float)((x - cx) * (x - cx) + (y - cy) * (y - cy))) / r;
      if(d >= 1) continue;
      const float f = (1 - d) * (1 - d);
      uint16_t &p = buf[y * LCD_W + x];
      p = gfxBlend(p, color, (uint8_t)(f * alpha));
    }
}

void gfxBlendRect(int x, int y, int w, int h, uint32_t color, uint8_t alpha)
{
  uint16_t *buf = (uint16_t *)canvas.getBuffer();
  for(int j = max(0, y); j < min(LCD_H, y + h); j++)
    for(int i = max(0, x); i < min(LCD_W, x + w); i++) { uint16_t &p = buf[j * LCD_W + i]; p = gfxBlend(p, color, alpha); }
}

void gfxBlendRoundRect(int x, int y, int w, int h, int r, uint32_t color, uint8_t alpha, uint32_t border, uint8_t borderAlpha)
{
  uint16_t *buf = (uint16_t *)canvas.getBuffer();
  for(int j = max(0, y); j < min(LCD_H, y + h); j++)
    for(int i = max(0, x); i < min(LCD_W, x + w); i++)
    {
      // distancia com sinal ate a borda arredondada (negativa = dentro)
      const float hx = w / 2.0f, hy = h / 2.0f;
      const float qx = fabsf(i + 0.5f - (x + hx)) - (hx - r), qy = fabsf(j + 0.5f - (y + hy)) - (hy - r);
      const float ox = max(qx, 0.0f), oy = max(qy, 0.0f);
      const float d = sqrtf(ox * ox + oy * oy) + min(max(qx, qy), 0.0f) - r;
      const float cov = constrain(0.5f - d, 0.0f, 1.0f);
      if(cov <= 0) continue;
      uint16_t &p = buf[j * LCD_W + i];
      p = gfxBlend(p, color, (uint8_t)(cov * alpha));
      if(borderAlpha)
      {
        const float b = constrain(1.0f - fabsf(d + 0.6f), 0.0f, 1.0f);
        if(b > 0) p = gfxBlend(p, border, (uint8_t)(b * borderAlpha));
      }
    }
}

void gfxShadeRows(int y0, int y1, uint32_t color, uint8_t a0, uint8_t a1)
{
  uint16_t *buf = (uint16_t *)canvas.getBuffer();
  for(int y = max(0, y0); y < min(LCD_H, y1); y++)
  {
    const uint8_t a = a0 + (a1 - a0) * (y - y0) / max(1, y1 - y0);
    for(int x = 0; x < LCD_W; x++) { uint16_t &p = buf[y * LCD_W + x]; p = gfxBlend(p, color, a); }
  }
}

static uint32_t mix(uint32_t a, uint32_t b, float t)
{
  const int r = ((a >> 16) & 255) + (((int)((b >> 16) & 255) - (int)((a >> 16) & 255)) * t);
  const int g = ((a >> 8) & 255) + (((int)((b >> 8) & 255) - (int)((a >> 8) & 255)) * t);
  const int bl = (a & 255) + (((int)(b & 255) - (int)(a & 255)) * t);
  return (r << 16) | (g << 8) | bl;
}

void gfxGradientV(uint32_t top, uint32_t mid, uint32_t bottom)
{
  for(int y = 0; y < LCD_H; y++)
  {
    const float t = y / (float)(LCD_H - 1);
    canvas.drawFastHLine(0, y, LCD_W, t < 0.6f ? mix(top, mid, t / 0.6f) : mix(mid, bottom, (t - 0.6f) / 0.4f));
  }
}

// ---- Cache de imagens decodificadas ----
struct ImgSlot { char name[24]; uint16_t *px; uint32_t used; };
// 5 fundos/mostradores (~250 KB cada, na PSRAM): trocar de tela nao precisa decodificar o JPEG de novo.
// (8 na v0.17.0 apertou demais a PSRAM; com pouca memoria, gfxTrimCaches solta os menos usados)
static ImgSlot slots[5];

void gfxTrimCaches(bool all)
{
  uint32_t now = millis();
  for(auto &s : slots)
  {
    if(!s.px) continue;
    if(!all && now - s.used < 10000) continue;   // em uso agora (ou ha pouco): fica
    heap_caps_free(s.px); s.px = nullptr; s.name[0] = 0;
  }
}

const uint16_t *gfxImageBuf(const char *name)
{
  if(!packOk()) return nullptr;
  for(auto &s : slots) if(s.px && !strcmp(s.name, name)) { s.used = millis(); return s.px; }
  ImgSlot *v = &slots[0];
  for(auto &s : slots) { if(!s.px) { v = &s; break; } if(s.used < v->used) v = &s; }
  if(!v->px) v->px = (uint16_t *)heap_caps_malloc(LCD_W * LCD_H * 2, MALLOC_CAP_SPIRAM);
  if(!v->px) return nullptr;
  size_t len = 0;
  const uint8_t *jpg = packFind(name, &len);
  if(!jpg) return nullptr;
  LGFX_Sprite sp;
  sp.setColorDepth(16);
  sp.setBuffer(v->px, LCD_W, LCD_H, 16);
  const bool good = sp.drawJpg(jpg, len, 0, 0, LCD_W, LCD_H);
  if(!good) { v->name[0] = 0; return nullptr; }
  strlcpy(v->name, name, sizeof(v->name));
  v->used = millis();
  return v->px;
}

bool gfxImage(const char *name)
{
  const uint16_t *p = gfxImageBuf(name);
  if(!p) return false;
  memcpy(canvas.getBuffer(), p, LCD_W * LCD_H * 2);
  return true;
}

const char *gfxRoman(int n, char *buf, size_t len)
{
  static const int V[] = {1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1};
  static const char *S[] = {"M", "CM", "D", "CD", "C", "XC", "L", "XL", "X", "IX", "V", "IV", "I"};
  buf[0] = 0;
  if(n <= 0 || n > 3999) { snprintf(buf, len, "%d", n); return buf; }
  for(int i = 0; i < 13; i++) while(n >= V[i]) { strlcat(buf, S[i], len); n -= V[i]; }
  return buf;
}

