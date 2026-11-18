// Temas do ID DMR com a bandeira do pais (incluido em Dmr.cpp).
// 2 = Bandeira de fundo, 3 = Faixa diagonal, 4 = Pixel 8-bit, 5 = Bandeira nas bordas

static uint32_t mixc(uint32_t a, uint32_t b, float t)
{
  t = constrain(t, 0.0f, 1.0f);
  const int r = ((a >> 16) & 255) + (((int)((b >> 16) & 255) - (int)((a >> 16) & 255)) * t);
  const int g = ((a >> 8) & 255) + (((int)((b >> 8) & 255) - (int)((a >> 8) & 255)) * t);
  const int bl = (a & 255) + (((int)(b & 255) - (int)(a & 255)) * t);
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

// bilinear na bandeira 120x90 (u, v de 0 a 1)
static uint32_t flagBilinear(const uint16_t *img, float u, float v)
{
  const float x = constrain(u * FLAG_W - 0.5f, 0.0f, FLAG_W - 1.001f), y = constrain(v * FLAG_H - 0.5f, 0.0f, FLAG_H - 1.001f);
  const int ix = (int)x, iy = (int)y; const float fx = x - ix, fy = y - iy;
  const float iw = 1.0f / FLAG_W, ih = 1.0f / FLAG_H;
  const uint32_t a = flagPixel(img, (ix + 0.5f) * iw, (iy + 0.5f) * ih), b = flagPixel(img, (ix + 1.5f) * iw, (iy + 0.5f) * ih);
  const uint32_t c = flagPixel(img, (ix + 0.5f) * iw, (iy + 1.5f) * ih), d = flagPixel(img, (ix + 1.5f) * iw, (iy + 1.5f) * ih);
  return mixc(mixc(a, b, fx), mixc(c, d, fx), fy);
}

// Fundo pronto por pais (montar a bandeira inteira custa; guarda o ultimo)
static uint16_t *themeBg = nullptr;
static char themeBgKey[8] = "";

static bool bgCached(const char *key)
{
  if(!themeBg) themeBg = (uint16_t *)heap_caps_malloc(LCD_W * LCD_H * 2, MALLOC_CAP_SPIRAM);
  if(!themeBg) return false;
  if(!strcmp(themeBgKey, key)) { memcpy(canvas.getBuffer(), themeBg, LCD_W * LCD_H * 2); return true; }
  return false;
}

static void bgStore(const char *key)
{
  if(!themeBg) return;
  memcpy(themeBg, canvas.getBuffer(), LCD_W * LCD_H * 2);
  strlcpy(themeBgKey, key, sizeof(themeBgKey));
}

static void statusText(char *b, size_t n, const Row &r, uint32_t since, bool upper)
{
  if(r.live) { const uint32_t e = (millis() - since) / 1000; snprintf(b, n, upper ? T("ON AIR %02lu:%02lu","NO AR %02lu:%02lu","EN AIRE %02lu:%02lu") : T("On air %02lu:%02lu","No ar %02lu:%02lu","En aire %02lu:%02lu"), (unsigned long)(e / 60), (unsigned long)(e % 60)); }
  else snprintf(b, n, upper ? T("LAST AT %s","ÚLTIMO ÀS %s","ÚLTIMO A LAS %s") : T("Last at %s","Último às %s","Último a las %s"), r.when[0] ? r.when : "--:--");
}

static void tgLine(char *b, size_t n, const Row &r, const Who &w)
{
  char tg[24]; tgText(tg, sizeof(tg), r.tg);
  if(w.id) snprintf(b, n, "%s%s%s · ID %lu", tg, r.slot[0] ? " · " : "", r.slot, (unsigned long)w.id);
  else snprintf(b, n, "%s%s%s", tg, r.slot[0] ? " · " : "", r.slot);
}

// Lista (toque longo) comum aos temas com bandeira
static void flagList(const Row *rs, const Who *info, int n, uint32_t title, uint32_t text, uint32_t muted, const uint8_t *font)
{
  gfxTextSpaced("ÚLTIMOS OUVIDOS", CX, 62, FxOsw14, title, 2);
  const int shown = min(n, 6);
  for(int i = 0; i < shown; i++)
  {
    const int y = 102 + i * 38;
    const int half = gfxRoundHalfWidth(y, 22);
    const int x0 = CX - half + 8;
    const bool fl = smallFlag(x0 + 13, y, 26, info[i].cc);
    gfxText(rs[i].call, x0 + (fl ? 32 : 0), y, font, rs[i].live ? 0xFF8A8Au : text, middle_left);
    if(info[i].name[0]) gfxTextFit(info[i].name, CX + 30, y, 70, FxInter13, muted);
    char b[16]; snprintf(b, sizeof(b), "%lu", (unsigned long)rs[i].tg);
    gfxText(b, CX + half - 8, y - 8, FxInter13, title, middle_right);
    gfxText(rs[i].when, CX + half - 8, y + 9, FxInter13, muted, middle_right);
  }
}

// "Cidade, UF · Pais"; se nao couber, so a cidade
static void placeLine(char *b, size_t n, const Who &w, const uint8_t *font, int maxW)
{
  if(w.place[0] && w.country[0] && strcmp(w.place, w.country))
  {
    snprintf(b, n, "%s · %s", w.place, w.country);
    if(gfxTextWidth(b, font) <= maxW) return;
  }
  strlcpy(b, w.place[0] ? w.place : w.country, n);
}

static void waiting(const char *st, uint32_t col)
{
  gfxTextSpaced("ID DMR", CX, 70, FxOsw14, col, 4);
  gfxWrapRoundCentered(st[0] ? st : T("Waiting…","Aguardando…","Esperando…"), CY, FontBody, 0xFFFFFFu, 28, 3, 40);
}

// ---- 2. Bandeira de fundo ----
static void drawFlagBg(const Row *rs, const Who *info, int n, const char *st, uint32_t since)
{
  const char *cc = n ? info[0].cc : "";
  char key[8]; snprintf(key, sizeof(key), "fb%s", cc);
  if(!bgCached(key))
  {
    const uint16_t *img = cc[0] ? flagImage(cc) : nullptr;
    uint16_t *buf = (uint16_t *)canvas.getBuffer();
    // a bandeira cobre o circulo (corta as laterais), escurecida e com vinheta
    const float sc = (float)LCD_H / FLAG_H;           // 4x na altura
    const float wpx = FLAG_W * sc;
    for(int y = 0; y < LCD_H; y++)
      for(int x = 0; x < LCD_W; x++)
      {
        const float dx = x - CX + 0.5f, dy = y - CY + 0.5f, d = sqrtf(dx * dx + dy * dy) / 180.0f;
        uint32_t c = img ? flagBilinear(img, (x + (wpx - LCD_W) / 2) / wpx, (y + 0.5f) / LCD_H) : 0x0B1A2Cu;
        c = mixc(c, 0x000000u, 0.42f + 0.38f * d * d);
        const uint16_t v = ((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c & 0xFF) >> 3);
        buf[y * LCD_W + x] = (v >> 8) | (v << 8);
      }
    // faixa escura atras do texto
    gfxShadeRows(110, 340, 0x000000u, 0, 120);
    bgStore(key);
  }
  if(!n) { waiting(st, 0xFFE680u); return; }
  char b[64];
  if(listView) { flagList(rs, info, n, 0xFFE680u, 0xFFFFFFu, 0xD8E0E8u, FxOsw18); return; }
  const Row &r = rs[0]; const Who &w = info[0];
  if(r.live)
  {
    const float el = (millis() - since) / 1000.0f;
    gfxArc(CX, CY, 170, 174, 0, 359.99f, 0xFFFFFFu, 50);
    gfxArc(CX, CY, 170, 174, 0, min(359.9f, el * 3.0f), 0xFFE680u, 255);
  }
  statusText(b, sizeof(b), r, since, true);
  gfxTextSpaced(b, CX, 96, FxOsw14, 0xFFE680u, 2);
  gfxTextShadow(r.call, CX, 156, FxOsw34, 0xFFFFFFu, middle_center, 0x000000);
  if(w.name[0]) gfxTextShadowFit(w.name, CX, 198, 260, FontTitle, 0xFFFFFFu, middle_center);
  placeLine(b, sizeof(b), w, FontSmall, 270);
  if(b[0]) gfxTextShadowFit(b, CX, 232, 270, FontSmall, 0xE6F0EAu, middle_center);
  tgLine(b, sizeof(b), r, w);
  gfxTextShadowFit(b, CX, 262, 250, FxInter13, 0xFFFFFFu, middle_center);
  if(r.dur && !r.live) { snprintf(b, sizeof(b), T("%d s on air","%d s no ar","%d s en aire"), r.dur); gfxTextShadow(b, CX, 290, FxInter13, 0xD8E0E8u, middle_center, 0x000000); }
}

// ---- 3. Faixa diagonal ----
static void drawSash(const Row *rs, const Who *info, int n, const char *st, uint32_t since)
{
  const char *cc = n ? info[0].cc : "";
  char key[8]; snprintf(key, sizeof(key), "sa%s", cc);
  if(!bgCached(key))
  {
    gfxGradientV(0x1A1A1Au, 0x121212u, 0x0A0A0Au);
    const uint16_t *img = cc[0] ? flagImage(cc) : nullptr;
    uint16_t *buf = (uint16_t *)canvas.getBuffer();
    const float ang = -28 * DEG_TO_RAD, ca = cosf(ang), sa = sinf(ang);
    const float bx = CX, by = 130, HW = 46, L = 260;   // meia largura da faixa e meio comprimento
    for(int y = 0; y < LCD_H; y++)
      for(int x = 0; x < LCD_W; x++)
      {
        const float dx = x + 0.5f - bx, dy = y + 0.5f - by;
        const float u = dx * ca + dy * sa, v = -dx * sa + dy * ca;   // ao longo / atravessando
        const float av = fabsf(v);
        uint16_t &q = buf[y * LCD_W + x];
        if(av > HW + 8) continue;
        if(av > HW) { q = gfxBlend(q, 0x000000u, (uint8_t)(110 * (1 - (av - HW) / 8))); continue; }   // sombra
        uint32_t c = img ? flagBilinear(img, (u + L) / (2 * L), (v + HW) / (2 * HW)) : 0x2A2A2Au;
        // dobra suave: claro no meio, escuro nas bordas
        c = mixc(c, 0x000000u, 0.12f + 0.25f * (av / HW) * (av / HW));
        q = gfxBlend(q, c, (uint8_t)(255 * constrain(HW - av + 0.5f, 0.0f, 1.0f)));
      }
    bgStore(key);
  }
  if(!n) { waiting(st, 0xBBBBBBu); return; }
  char b[64];
  if(listView) { gfxShadeRows(0, LCD_H, 0x000000u, 190, 190); flagList(rs, info, n, 0xFFCE5Au, 0xFFFFFFu, 0xA8A8A8u, FxOsw18); return; }
  const Row &r = rs[0]; const Who &w = info[0];
  statusText(b, sizeof(b), r, since, true);
  if(r.live) { canvas.fillSmoothCircle(CX - gfxTextWidth(b, FxOsw14) / 2 - 14, 40, 4, ((millis() / 500) & 1) ? 0xFF4A4Au : 0x7A2020u); }
  gfxTextSpaced(b, CX, 40, FxOsw14, r.live ? 0xFF9A8Au : 0xBBBBBBu, 2);
  gfxText(r.call, CX, 252, FxOsw34, 0xFFFFFFu);
  if(w.name[0]) gfxTextFit(w.name, CX, 286, 230, FontBody, 0xFFCE5Au);
  placeLine(b, sizeof(b), w, FxInter13, 200);
  if(b[0]) gfxTextFit(b, CX, 310, 200, FxInter13, 0xC8C8C8u);
  tgLine(b, sizeof(b), r, w);
  gfxTextFit(b, CX, 330, 160, FxInter13, 0x8C8C8Cu);
}

// ---- 4. Pixel art 8-bit ----
static void drawPixel(const Row *rs, const Who *info, int n, const char *st, uint32_t since)
{
  const char *cc = n ? info[0].cc : "";
  char key[8]; snprintf(key, sizeof(key), "px%s", cc);
  const int CW = 24, CH = 18, P = 8;   // celulas da bandeira e tamanho do "pixel"
  const int fx0 = CX - CW * P / 2, fy0 = 46;
  if(!bgCached(key))
  {
    canvas.fillScreen(0x0E0E1Cu);
    for(int y = 0; y < LCD_H; y += 3) canvas.drawFastHLine(0, y, LCD_W, 0x14142Au);   // linhas de tubo
    const uint16_t *img = cc[0] ? flagImage(cc) : nullptr;
    if(img)
      for(int j = 0; j < CH; j++)
        for(int i = 0; i < CW; i++)
        {
          // media da celula (a bandeira vira pixels de verdade)
          int R = 0, G = 0, B = 0;
          for(int k = 0; k < 4; k++)
          {
            const uint32_t c = flagPixel(img, (i + 0.25f + 0.5f * (k & 1)) / CW, (j + 0.25f + 0.5f * (k >> 1)) / CH);
            R += (c >> 16) & 255; G += (c >> 8) & 255; B += c & 255;
          }
          // paleta "8 bits": 3-3-2 niveis
          R = (R / 4) & 0xE0; G = (G / 4) & 0xE0; B = (B / 4) & 0xC0;
          canvas.fillRect(fx0 + i * P, fy0 + j * P, P - 1, P - 1, ((uint32_t)R << 16) | (G << 8) | B);
        }
    else
      for(int j = 0; j < CH; j++) for(int i = 0; i < CW; i++) canvas.fillRect(fx0 + i * P, fy0 + j * P, P - 1, P - 1, ((i + j) & 1) ? 0x22224Au : 0x1A1A3Au);
    bgStore(key);
  }
  if(!n) { gfxText(T("DMR ID","ID DMR","ID DMR"), CX, 214, FxVT22, 0x7CFC00u); gfxWrapRoundCentered(st[0] ? st : T("Waiting…","Aguardando…","Esperando…"), 260, FxVT22, 0xFFFFFFu, 24, 3, 40); return; }
  char b[64];
  if(listView) { canvas.fillRect(0, 40, LCD_W, 160, 0x0E0E1Cu); flagList(rs, info, n, 0xFFCC00u, 0x7CFC00u, 0xC0C0C0u, FxVT22); return; }
  const Row &r = rs[0]; const Who &w = info[0];
  gfxText(r.call, CX, 222, FxVT48, 0x7CFC00u);
  char up[32]; strlcpy(up, w.name[0] ? w.name : w.country, sizeof(up));
  for(char *p = up; *p; p++) if(*p >= 'a' && *p <= 'z') *p -= 32;
  if(w.id) snprintf(b, sizeof(b), "%s  ID %lu", up, (unsigned long)w.id); else strlcpy(b, up, sizeof(b));
  gfxTextFit(b, CX, 258, 260, FxVT22, 0xFFFFFFu);
  if(w.place[0]) gfxTextFit(w.place, CX, 280, 240, FxVT22, 0x9A9AC8u);
  if(r.live)
  {
    const uint32_t e = (millis() - since) / 1000;
    snprintf(b, sizeof(b), "> TG %lu  %02lu:%02lu%s", (unsigned long)r.tg, (unsigned long)(e / 60), (unsigned long)(e % 60), ((millis() / 400) & 1) ? "_" : " ");
  }
  else snprintf(b, sizeof(b), "> TG %lu  %s", (unsigned long)r.tg, r.when[0] ? r.when : "--:--");
  gfxTextFit(b, CX, 306, 220, FxVT22, 0xFFCC00u);
}

// ---- 5. Bandeira nas bordas ----
static void drawRing(const Row *rs, const Who *info, int n, const char *st, uint32_t since)
{
  gfxGradientV(0x0E1226u, 0x0A0D1Cu, 0x070912u);
  const char *cc = n ? info[0].cc : "";
  const uint16_t *img = cc[0] ? flagImage(cc) : nullptr;
  uint32_t col[3] = {0x3C6EFFu, 0xFFFFFFu, 0xE8C77Au};
  int nc = img ? flagColors(img, col, 3) : 3;
  if(nc < 2) { col[1] = mixc(col[0], 0xFFFFFFu, 0.6f); nc = 2; }
  const bool live = n && rs[0].live;
  const float rot = live ? fmodf(millis() / 40.0f, 360.0f) : 0;
  const int segs = 18;
  const float step = 360.0f / segs;
  for(int k = 0; k < segs; k++)
  {
    const float a0 = rot + k * step + 1.5f, a1 = rot + (k + 1) * step - 1.5f;
    gfxArc(CX, CY, 164, 174, fmodf(a0, 360.0f), fmodf(a0, 360.0f) + (a1 - a0), col[k % nc], 255, 1.0f);
  }
  gfxArc(CX, CY, 154, 157, 0, 359.99f, col[nc > 2 ? 2 : 0], 200);
  if(!n) { waiting(st, col[0]); return; }
  char b[64];
  if(listView) { flagList(rs, info, n, mixc(col[0], 0xFFFFFFu, 0.5f), 0xFFFFFFu, 0xA9B3D6u, FxOsw18); return; }
  const Row &r = rs[0]; const Who &w = info[0];
  if(smallFlag(CX, 96, 64, w.cc)) canvas.drawRoundRect(CX - 33, 96 - 25, 66, 50, 7, 0xFFFFFFu);
  else canvas.fillSmoothCircle(CX, 96, 18, 0x1C2440u);
  gfxText(r.call, CX, 162, FxOsw34, 0xFFFFFFu);
  if(w.name[0]) gfxTextFit(w.name, CX, 200, 240, FontTitle, mixc(col[0], 0xFFFFFFu, 0.55f));
  placeLine(b, sizeof(b), w, FontSmall, 250);
  if(b[0]) gfxTextFit(b, CX, 232, 250, FontSmall, 0xC9D2FFu);
  tgLine(b, sizeof(b), r, w);
  gfxTextFit(b, CX, 260, 240, FxInter13, 0x8890B8u);
  statusText(b, sizeof(b), r, since, true);
  gfxTextSpaced(b, CX, 292, FxOsw14, live ? 0xFF8A7Au : 0xA9B3D6u, 2);
}
