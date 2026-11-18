// Temas extras das telas Proximas horas e Proximos dias (incluido em Weather.cpp)

static uint32_t heatCol(float t)
{
  if(t < 12) return mixc(0x4F7CFFu, 0x3FC8FFu, t / 12.0f);
  if(t < 20) return mixc(0x3FC8FFu, 0x47E6B5u, (t - 12) / 8.0f);
  if(t < 26) return mixc(0x47E6B5u, 0xFFC94Fu, (t - 20) / 6.0f);
  return mixc(0xFFC94Fu, 0xFF5A3Du, (t - 26) / 9.0f);
}


// cor do ceu numa hora (0..24, fracionaria) - escurece/acinzenta com nuvem e chuva
static uint32_t skyAt(float h, int code)
{
  struct K { float h; uint32_t c; };
  static const K keys[] = {{0, 0x0B1030u}, {5, 0x141A44u}, {6, 0x6A4A7Au}, {7, 0xF2A65Au}, {8, 0x86B8F0u}, {12, 0x4F8FE0u},
                           {16, 0x5B9BE6u}, {17.3f, 0xF2A65Au}, {18.2f, 0xC2577Au}, {19, 0x3A2F6Au}, {20, 0x141A3Au}, {24, 0x0B1030u}};
  int i = 0; while(i < 10 && h >= keys[i + 1].h) i++;
  uint32_t c = mixc(keys[i].c, keys[i + 1].c, (h - keys[i].h) / (keys[i + 1].h - keys[i].h));
  if(rainy(code) || code >= 95) c = mixc(c, 0x3C4654u, 0.55f);
  else if(code == 3 || code == 45 || code == 48) c = mixc(c, 0x6A7480u, 0.45f);
  else if(code == 2) c = mixc(c, 0x8C98A6u, 0.2f);
  return c;
}

// ---- Proximas horas: faixa do ceu ----
static void drawHoursSky(const WxNow &n, const WxHour *hs, int nh)
{
  gfxGradientV(0x0C1020u, 0x0A0E1Cu, 0x070A14u);
  gfxText(T("Next hours","Próximas horas","Próximas horas"), CX, 52, FontSmall, 0xC9D2FFu);
  const int COLS = min(7, (nh + 1) / 2);   // de 2 em 2 horas
  const int X0 = 28, X1 = 332, Y0 = 92, Y1 = 236;
  const float colW = (float)(X1 - X0) / COLS;
  uint16_t *buf = (uint16_t *)canvas.getBuffer();
  const float e = intro();
  // ceu continuo: interpola a cor entre os centros das colunas
  uint32_t colC[7];
  for(int k = 0; k < COLS; k++) { struct tm tk; localtime_r(&hs[k * 2].t, &tk); colC[k] = skyAt(tk.tm_hour + 0.5f, hs[k * 2].code); }
  for(int x = X0; x < X1; x++)
  {
    const float f = (x - X0) / colW - 0.5f;
    const int i = constrain((int)floorf(f), 0, COLS - 1), j = min(COLS - 1, i + 1);
    const float fr = constrain(f - i, 0.0f, 1.0f);
    const uint32_t c = mixc(colC[i], colC[j], fr);
    const int half = gfxRoundHalfWidth((Y0 + Y1) / 2, 0);
    if(abs(x - CX) > half) continue;
    for(int y = Y0; y < Y1; y++)
    {
      const float v = (float)(y - Y0) / (Y1 - Y0);
      const uint32_t cc = mixc(mixc(c, 0xFFFFFFu, 0.10f * (1 - v)), 0x000000u, 0.25f * v);   // mais claro em cima
      const uint8_t a = (uint8_t)(255 * constrain(e * 1.4f - (float)(x - X0) / (X1 - X0) * 0.4f, 0.0f, 1.0f));
      uint16_t &p = buf[y * LCD_W + x]; p = gfxBlend(p, cc, a);
    }
  }
  // cantos arredondados da faixa
  gfxBlendRoundRect(X0, Y0, X1 - X0, Y1 - Y0, 14, 0x000000u, 0, 0xFFFFFFu, 40);
  char b[48];
  for(int k = 0; k < COLS; k++)
  {
    const WxHour &h = hs[k * 2];
    const int cx = X0 + (int)(colW * (k + 0.5f));
    struct tm t; localtime_r(&h.t, &t);
    weatherIcon(cx, 128, 30, h.code, h.day, 0x3A4A6Au);
    snprintf(b, sizeof(b), "%d°", (int)lroundf(h.temp));
    gfxTextShadow(b, cx, 178, FontBody, 0xFFFFFFu, middle_center, 0x000000);
    if(h.prob >= 20)
    {
      const int hh = (int)(h.prob * 0.3f * e);
      gfxBlendRoundRect(cx - 9, Y1 - 6 - hh, 18, hh, 4, 0x6EC1FFu, 160);
      snprintf(b, sizeof(b), "%d%%", h.prob);
      gfxText(b, cx, Y1 - 14 - hh, FxInter13, 0xD8ECFFu);
    }
    snprintf(b, sizeof(b), k == 0 ? T("Now","Agora","Ahora") : "%dh", t.tm_hour);
    gfxText(b, cx, 254, FxInter13, k == 0 ? 0xFFFFFFu : 0xA9B3D6u);
  }
  // resumo
  int first = -1; for(int i = 0; i < nh; i++) if(hs[i].prob >= 50) { first = i; break; }
  if(first >= 0) { struct tm t; localtime_r(&hs[first].t, &t); snprintf(b, sizeof(b), T("Rain from %dh","Chuva a partir das %dh","Lluvia desde las %dh"), t.tm_hour); }
  else strlcpy(b, T("No rain in sight","Sem chuva à vista","Sin lluvia a la vista"), sizeof(b));
  gfxTextFit(b, CX, 288, 220, FontSmall, first >= 0 ? 0x6EC1FFu : 0xC9D2FFu);
  snprintf(b, sizeof(b), "Agora %d° · %s", (int)lroundf(n.temp), weatherConditionShort(n.code));
  gfxTextFit(b, CX, 314, 200, FxInter13, 0x8890B8u);
}

// ---- Proximas horas: cartoes (uma hora em destaque, as seguintes embaixo) ----
static void drawHoursCards(const WxNow &n, const WxHour *hs, int nh)
{
  if(!gfxImage("bg_obsidian")) gfxBackground();
  const WxHour &f = hs[1];   // proxima hora cheia
  struct tm t; localtime_r(&f.t, &t);
  char b[48];
  snprintf(b, sizeof(b), "%dh", t.tm_hour);
  gfxTextSpaced(b, CX, 50, FxOsw14, 0xA9C4BEu, 2);
  weatherIcon(CX, 96, 60, f.code, f.day, 0x0A1A18u);
  snprintf(b, sizeof(b), "%d°", (int)lroundf(f.temp));
  gfxText(b, CX + 6, 162, FxClockL, heatCol(f.temp));
  if(f.prob >= 10 || f.mm >= 0.1f) snprintf(b, sizeof(b), T("%d%% rain · %.1f mm","%d%% de chuva · %.1f mm","%d%% de lluvia · %.1f mm"), f.prob, f.mm);
  else snprintf(b, sizeof(b), "%s · sem chuva", weatherConditionShort(f.code));
  gfxTextFit(b, CX, 210, 230, FxInter13, f.prob >= 50 ? 0x6EC1FFu : 0xC8D6D2u);
  // 4 cartoes: de 2 em 2 horas depois do destaque
  const int CW = 60, CH = 64, GAP = 6, Y = 232;
  const int total = 4 * CW + 3 * GAP, x0 = CX - total / 2;
  const float e = intro();
  for(int k = 0; k < 4; k++)
  {
    const int idx = min(nh - 1, 3 + k * 2);
    const WxHour &h = hs[idx];
    const int x = x0 + k * (CW + GAP);
    const int dy = (int)((1 - constrain(e * 1.5f - k * 0.15f, 0.0f, 1.0f)) * 30);   // sobem um depois do outro
    gfxBlendRoundRect(x, Y + dy, CW, CH, 10, 0x13252Au, 230, 0x2C5A52u, 200);
    struct tm tt; localtime_r(&h.t, &tt);
    snprintf(b, sizeof(b), "%dh", tt.tm_hour);
    gfxText(b, x + CW / 2, Y + dy + 12, FxInter13, 0x8FB0A8u);
    weatherIcon(x + CW / 2, Y + dy + 31, 22, h.code, h.day, 0x13252Au);
    snprintf(b, sizeof(b), "%d°", (int)lroundf(h.temp));
    gfxText(b, x + CW / 2, Y + dy + 52, FontSmall, heatCol(h.temp));
    if(h.prob >= 30) canvas.fillSmoothCircle(x + CW - 9, Y + dy + 9, 3, 0x6EC1FFu);
  }
  snprintf(b, sizeof(b), "Agora %d° · %s", (int)lroundf(n.temp), weatherConditionShort(n.code));
  gfxTextFit(b, CX, 316, 190, FxInter13, 0x6C8C86u);
}

// ---- Proximos dias: colunas de minima a maxima ----
static void drawDaysColumns(const WxDay *ds, int nd)
{
  gfxGradientV(0x0B1E24u, 0x081A1Fu, 0x051014u);
  gfxTextSpaced("PRÓXIMOS DIAS", CX, 50, FxOsw14, 0x9FE8D3u, 3);
  const int N = min(nd, 5);
  float lo = 99, hi = -99;
  for(int i = 0; i < N; i++) { lo = min(lo, ds[i].tmin); hi = max(hi, ds[i].tmax); }
  if(hi - lo < 8) { const float m = (hi + lo) / 2; lo = m - 4; hi = m + 4; }
  const int YT = 136, YB = 236, step = 58, x0 = CX - step * (N - 1) / 2;
  const float e = intro();
  auto ty = [&](float t) { return YB - (t - lo) / (hi - lo) * (YB - YT); };
  char b[24];
  for(int i = 0; i < N; i++)
  {
    const WxDay &d = ds[i];
    const int x = x0 + i * step;
    const float g = constrain(e * 1.6f - i * 0.12f, 0.0f, 1.0f);   // colunas crescem uma depois da outra
    const float yTop = ty(d.tmax), yBot = ty(d.tmin);
    const float mid = (yTop + yBot) / 2;
    const float a = mid - (mid - yTop) * g, z = mid + (yBot - mid) * g;
    // trilho e coluna com degrade da minima (embaixo) para a maxima (em cima)
    canvas.fillSmoothRoundRect(x - 6, YT - 6, 12, YB - YT + 12, 6, 0x10303Au);
    for(int y = (int)a; y <= (int)z; y++)
    {
      const float t = d.tmin + (d.tmax - d.tmin) * (z > a ? (z - y) / (z - a) : 0);
      const uint32_t c = heatCol(t);
      const float dd = constrain(min(y - a, z - y), 0.0f, 5.0f);   // pontas arredondadas
      const int w = max(1, (int)sqrtf(max(0.0f, 25 - (5 - dd) * (5 - dd))));
      canvas.drawFastHLine(x - w, y, w * 2, c);
    }
    weatherIcon(x, 84, 30, d.code, true, 0x0B1E24u);
    snprintf(b, sizeof(b), "%d°", (int)lroundf(d.tmax));
    gfxText(b, x, (int)a - 14, FontSmall, 0xFFFFFFu);
    snprintf(b, sizeof(b), "%d°", (int)lroundf(d.tmin));
    gfxText(b, x, (int)z + 14, FxInter13, 0x9AB8C0u);
    struct tm t; gmtime_r(&d.date, &t);
    gfxText(i == 0 ? T("Today","Hoje","Hoy") : WD[uiLang][t.tm_wday], x, 278, FxInter13, i == 0 ? 0xFFFFFFu : 0xC8DDE2u);
    if(d.rainProb >= 30) { snprintf(b, sizeof(b), "%d%%", d.rainProb); gfxText(b, x, 296, FxInter13, 0x6EC1FFu); }
  }
}
