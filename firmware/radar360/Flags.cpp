// Bandeiras dos paises (tools/flags_pack.py): pais pelo cadastro do RadioID ou pelo prefixo do indicativo.
#include "Flags.h"
#include "Pack.h"
#include "Gfx.h"
#include <esp_heap_caps.h>
#include <string.h>
#include <ctype.h>

static bool lineField(const char *&p, const char *end, char *out, size_t n, char sep)
{
  size_t k = 0;
  while(p < end && *p != sep && *p != '\n') { if(k + 1 < n) out[k++] = *p; p++; }
  out[k] = 0;
  if(p < end && *p == sep) { p++; return true; }
  return false;
}

static bool eqNoCase(const char *a, const char *b)
{
  for(; *a && *b; a++, b++) if(tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
  return !*a && !*b;
}

bool flagCountryByName(const char *en, char cc[3], char *namePt, size_t n)
{
  if(!en || !en[0]) return false;
  size_t len = 0;
  const char *p = (const char *)packFind("countries", &len);
  if(!p) return false;
  const char *end = p + len;
  while(p < end)
  {
    char code[4], name[48], pt[48];
    lineField(p, end, code, sizeof(code), '|');
    lineField(p, end, name, sizeof(name), '|');
    lineField(p, end, pt, sizeof(pt), '|');
    while(p < end && *p != '\n') p++;
    if(p < end) p++;
    if(eqNoCase(name, en)) { strlcpy(cc, code, 3); if(namePt) strlcpy(namePt, pt, n); return true; }
  }
  return false;
}

bool flagCountryByCode(const char *cc, char *namePt, size_t n)
{
  size_t len = 0;
  const char *p = (const char *)packFind("countries", &len);
  if(!p || !cc || !cc[0]) return false;
  const char *end = p + len;
  while(p < end)
  {
    char code[4], name[48], pt[48];
    lineField(p, end, code, sizeof(code), '|');
    lineField(p, end, name, sizeof(name), '|');
    lineField(p, end, pt, sizeof(pt), '|');
    while(p < end && *p != '\n') p++;
    if(p < end) p++;
    if(!strcmp(code, cc)) { if(namePt) strlcpy(namePt, pt, n); return true; }
  }
  return false;
}

bool flagCountryByCall(const char *call, char cc[3])
{
  size_t len = 0;
  const char *p = (const char *)packFind("callprefix", &len);
  if(!p || !call || !call[0]) return false;
  const char *end = p + len;
  // as linhas ja vem do prefixo mais longo para o mais curto: a primeira que casar vale
  while(p < end)
  {
    char pre[8], code[4];
    lineField(p, end, pre, sizeof(pre), ' ');
    lineField(p, end, code, sizeof(code), ' ');
    while(p < end && *p != '\n') p++;
    if(p < end) p++;
    const size_t k = strlen(pre);
    if(k && !strncasecmp(call, pre, k)) { strlcpy(cc, code, 3); return true; }
  }
  return false;
}

// ---- Imagens: 120x90, decodificadas sob demanda e guardadas (as 4 ultimas) ----
struct Slot { char cc[3]; uint16_t *px; uint32_t used; };
static Slot slots[4] = {};

const uint16_t *flagImage(const char *cc)
{
  if(!cc || !cc[0]) return nullptr;
  static uint32_t tick = 0;
  for(auto &s : slots) if(s.px && !strncmp(s.cc, cc, 2)) { s.used = ++tick; return s.px; }
  size_t len = 0;
  const uint8_t *b = packFind("flags", &len);
  if(!b || len < 6 || memcmp(b, "FLG1", 4)) return nullptr;
  uint16_t n; memcpy(&n, b + 4, 2);
  const uint8_t *png = nullptr; uint16_t plen = 0;
  for(int i = 0; i < n; i++)
  {
    const uint8_t *e = b + 6 + i * 8;
    if(e[0] == cc[0] && e[1] == cc[1])
    {
      uint32_t off; memcpy(&off, e + 2, 4); memcpy(&plen, e + 6, 2);
      if(off + plen <= len) png = b + off;
      break;
    }
  }
  if(!png) return nullptr;
  Slot *v = &slots[0];
  for(auto &s : slots) { if(!s.px) { v = &s; break; } if(s.used < v->used) v = &s; }
  if(!v->px) v->px = (uint16_t *)heap_caps_malloc(FLAG_W * FLAG_H * 2, MALLOC_CAP_SPIRAM);
  if(!v->px) return nullptr;
  LGFX_Sprite sp; sp.setColorDepth(16); sp.setBuffer(v->px, FLAG_W, FLAG_H, 16);
  sp.fillScreen(0x000000u);
  sp.drawPng(png, plen, 0, 0);
  strlcpy(v->cc, cc, sizeof(v->cc));
  v->used = ++tick;
  return v->px;
}

uint32_t flagPixel(const uint16_t *img, float u, float v)
{
  int x = (int)(u * FLAG_W), y = (int)(v * FLAG_H);
  x = x < 0 ? 0 : x >= FLAG_W ? FLAG_W - 1 : x;
  y = y < 0 ? 0 : y >= FLAG_H ? FLAG_H - 1 : y;
  uint16_t s = img[y * FLAG_W + x]; s = (s >> 8) | (s << 8);
  return ((uint32_t)((s >> 11) << 3) << 16) | ((uint32_t)(((s >> 5) & 63) << 2) << 8) | ((s & 31) << 3);
}

// cores principais (para o anel): as mais frequentes, ignorando quase-iguais
int flagColors(const uint16_t *img, uint32_t *out, int maxN)
{
  struct C { uint32_t rgb; int n; } c[24]; int k = 0;
  for(int y = 2; y < FLAG_H; y += 4)
    for(int x = 2; x < FLAG_W; x += 4)
    {
      const uint32_t p = flagPixel(img, (x + 0.5f) / FLAG_W, (y + 0.5f) / FLAG_H);
      int j = 0;
      for(; j < k; j++)
      {
        const int dr = (int)((c[j].rgb >> 16) & 255) - (int)((p >> 16) & 255), dg = (int)((c[j].rgb >> 8) & 255) - (int)((p >> 8) & 255), db = (int)(c[j].rgb & 255) - (int)(p & 255);
        if(dr * dr + dg * dg + db * db < 2400) { c[j].n++; break; }
      }
      if(j == k && k < 24) c[k++] = {p, 1};
    }
  for(int i = 0; i < k; i++) for(int j = i + 1; j < k; j++) if(c[j].n > c[i].n) { C t = c[i]; c[i] = c[j]; c[j] = t; }
  int m = 0;
  for(int i = 0; i < k && m < maxN; i++) if(c[i].n >= 6) out[m++] = c[i].rgb;
  return m;
}
