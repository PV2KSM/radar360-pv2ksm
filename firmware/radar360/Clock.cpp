#include "Clock.h"
#include "Lang.h"
#include "Gfx.h"
#include "Config.h"
#include "Feat.h"
#include "Net.h"
#include "Sky.h"
#include "Weather.h"
#include "Pack.h"
#include <time.h>
#include <math.h>

static const char *const WEEKDAYS[3][7] = {
  {"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"},
  {"domingo","segunda-feira","terça-feira","quarta-feira","quinta-feira","sexta-feira","sábado"},
  {"domingo","lunes","martes","miércoles","jueves","viernes","sábado"}};
static const char *const WD3[] = {"DOM", "SEG", "TER", "QUA", "QUI", "SEX", "SÁB"};
static const char *const wd3[3][7] = {
  {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"},
  {"dom","seg","ter","qua","qui","sex","sáb"},
  {"dom","lun","mar","mié","jue","vie","sáb"}};
static const char *const MONTHS[3][12] = {
  {"January","February","March","April","May","June","July","August","September","October","November","December"},
  {"janeiro","fevereiro","março","abril","maio","junho","julho","agosto","setembro","outubro","novembro","dezembro"},
  {"enero","febrero","marzo","abril","mayo","junio","julio","agosto","septiembre","octubre","noviembre","diciembre"}};
static const char *const MON3[] = {"JAN", "FEV", "MAR", "ABR", "MAI", "JUN", "JUL", "AGO", "SET", "OUT", "NOV", "DEZ"};
static const char *const mon3[3][12] = {
  {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"},
  {"jan","fev","mar","abr","mai","jun","jul","ago","set","out","nov","dez"},
  {"ene","feb","mar","abr","may","jun","jul","ago","sep","oct","nov","dic"}};
static const char *const STYLE_NAMES[] = {"Realista", "Clássico", "Anel 24 h", "Minimal", "Cockpit", "Simples",
  "Art Déco", "Relógio de bolso", "Rádio valvulado", "Relógio de bordo", "Altímetro", "Horizonte artificial", "Bússola HSI"};

const char *clockStyleName(uint8_t s) { return s < CLK_COUNT ? STYLE_NAMES[s] : "?"; }

static uint32_t mixc(uint32_t a, uint32_t b, float t)
{
  t = constrain(t, 0.0f, 1.0f);
  const int r = ((a >> 16) & 255) + (((int)((b >> 16) & 255) - (int)((a >> 16) & 255)) * t);
  const int g = ((a >> 8) & 255) + (((int)((b >> 8) & 255) - (int)((a >> 8) & 255)) * t);
  const int bl = (a & 255) + (((int)(b & 255) - (int)(a & 255)) * t);
  return (r << 16) | (g << 8) | bl;
}
static uint32_t tempColor(float t)
{
  if(t < 22) return mixc(0x7FD8FFu, 0xFFC857u, (t - 12) / 10.0f);
  return mixc(0xFFC857u, 0xFF6B6Bu, (t - 22) / 12.0f);
}

struct Now { struct tm t; bool valid; WxNow w; bool wx; };
static Now snap()
{
  Now n;
  const time_t now = time(nullptr);
  localtime_r(&now, &n.t);
  n.valid = netTimeValid();
  n.wx = weatherNow(&n.w);
  return n;
}

static void netBadge(int y, uint32_t col)
{
  if(netConnected()) return;
  gfxTextShadow(netApActive() ? T("Setup mode","Modo configuração","Modo configuración") : T("No network","Sem rede","Sin red"), CX, y, FontSmall, col);
}

// ---- Realista ----
static void drawReal(const Now &n)
{
  skyDraw(true);
  char b[64];
  netBadge(56, 0xFFD65Au);
  if(n.valid) snprintf(b, sizeof(b), T("%s, %d %s","%s, %d de %s","%s, %d de %s"), WEEKDAYS[uiLang][n.t.tm_wday], n.t.tm_mday, MONTHS[uiLang][n.t.tm_mon]);
  else strlcpy(b, "Aguardando a hora…", sizeof(b));
  gfxTextShadow(b, CX, 86, FontSmall, 0xF2F5F8u);
  if(n.valid) snprintf(b, sizeof(b), "%02d:%02d", n.t.tm_hour, n.t.tm_min); else strlcpy(b, "--:--", sizeof(b));
  gfxTextShadow(b, CX, 150, FxClockXL, 0xFFFFFFu);
  if(cfg.showSeconds && n.valid)
  {
    gfxArc(CX, CY, 171, 174, 0, 359.99f, 0xFFFFFFu, 40);
    if(n.t.tm_sec) gfxArc(CX, CY, 171, 174, 0, n.t.tm_sec * 6, 0xFFFFFFu, 190);
  }
  if(n.wx)
  {
    snprintf(b, sizeof(b), "%d° · %s", (int)lroundf(n.w.temp), weatherConditionShort(n.w.code));
    gfxTextShadow(b, CX, 216, FontBody, 0xFFFFFFu);
    char t1[40], t2[48];
    if(weatherAlert(t1, sizeof(t1), t2, sizeof(t2))) gfxTextShadow(t1, CX, 243, FontSmall, 0xBFD8FFu);
    else { snprintf(b, sizeof(b), T("Max %d°  ·  Min %d°","Máx %d°  ·  Mín %d°","Máx %d°  ·  Mín %d°"), (int)lroundf(n.w.tmax), (int)lroundf(n.w.tmin)); gfxTextShadow(b, CX, 243, FontSmall, 0xE6ECF2u); }
  }
}

// ---- Classico (mostrador dourado) ----
static void hand(float deg, float len, float tail, float r0, float r1, uint32_t col, uint32_t shadow)
{
  const float a = (deg - 90) * DEG_TO_RAD, ca = cosf(a), sa = sinf(a);
  const int x0 = CX - ca * tail, y0 = CY - sa * tail, x1 = CX + ca * len, y1 = CY + sa * len;
  if(shadow != 0xFFFFFFFF) canvas.drawWedgeLine(x0 + 2, y0 + 3, x1 + 2, y1 + 3, r0, r1, shadow);
  canvas.drawWedgeLine(x0, y0, x1, y1, r0, r1, col);
}

static void classicFallback()
{
  canvas.fillScreen(0x07071Au);
  canvas.drawCircle(CX, CY, 170, 0xF2C66Du); canvas.drawCircle(CX, CY, 163, 0x967A46u);
  for(int i = 0; i < 60; i++)
  {
    const float a = (i * 6 - 90) * DEG_TO_RAD; const bool big = i % 5 == 0;
    canvas.drawWideLine(CX + cosf(a) * (big ? 144 : 150), CY + sinf(a) * (big ? 144 : 150), CX + cosf(a) * 158, CY + sinf(a) * 158, big ? 1.6f : 0.6f, big ? 0xF2C66Du : 0x967A46u);
  }
  canvas.drawCircle(CX, 270, 38, 0x967A46u);
}

static void drawClassic(const Now &n)
{
  const uint32_t GOLD = 0xF2C66Du;
  if(!gfxImage("dial_classic")) classicFallback();
  char b[32];
  if(n.valid)
  {
    snprintf(b, sizeof(b), "%s %d %s", WD3[n.t.tm_wday], n.t.tm_mday, MON3[n.t.tm_mon]);
    gfxTextSpaced(b, CX, 98, FxCinzel14, 0xC9B98Eu, 2);
  }
  if(n.wx)
  {
    weatherIcon(CX, 258, 30, n.w.code, n.w.day, 0x0A0A20u);
    snprintf(b, sizeof(b), "%d°", (int)lroundf(n.w.temp));
    gfxText(b, CX + 2, 287, FxCinzel20, 0xF6F3FFu);
  }
  netBadge(126, 0xFFD65Au);
  if(!n.valid) return;
  const float s = n.t.tm_sec, m = n.t.tm_min + s / 60.0f, h = (n.t.tm_hour % 12) + m / 60.0f;
  hand(h * 30, 88, 16, 5.0f, 1.6f, 0xF6F3FFu, 0x04040Eu);
  hand(m * 6, 134, 18, 3.6f, 1.0f, 0xF6F3FFu, 0x04040Eu);
  hand(s * 6, 142, 30, 1.1f, 0.8f, GOLD, 0xFFFFFFFF);
  const float a = (s * 6 - 90) * DEG_TO_RAD;
  canvas.fillSmoothCircle(CX - cosf(a) * 26, CY - sinf(a) * 26, 4, GOLD);
  canvas.fillSmoothCircle(CX, CY, 7, GOLD);
  canvas.fillSmoothCircle(CX, CY, 2, 0x0B0C24u);
}

// ---- Anel 24 h ----
static void drawRing(const Now &n)
{
  if(!gfxImage("bg_obsidian")) gfxBackground();
  WxHour hs[24]; const int nh = weatherHours(hs, 24);
  float temps[24]; int prob[24]; bool have[24];
  for(int i = 0; i < 24; i++) have[i] = false;
  for(int i = 0; i < nh; i++)
  {
    struct tm t; localtime_r(&hs[i].t, &t);
    temps[t.tm_hour] = hs[i].temp; prob[t.tm_hour] = hs[i].prob; have[t.tm_hour] = true;
  }
  for(int h = 0; h < 24; h++)
  {
    const float a0 = h * 15 + 1.2f;
    gfxArc(CX, CY, 152, 166, a0, a0 + 12.6f, have[h] ? tempColor(temps[h]) : 0x1E3A35u, have[h] ? 255 : 200);
    if(have[h] && prob[h] >= 50)
    {
      const float a = (h * 15 + 7.5f - 90) * DEG_TO_RAD;
      canvas.fillSmoothCircle(CX + cosf(a) * 142, CY + sinf(a) * 142, 2.6f, 0x5AA9FFu);
    }
  }
  static const char *const L[] = {"0h", "6h", "12h", "18h"};
  for(int k = 0; k < 4; k++)
  {
    const float a = (k * 90 - 90) * DEG_TO_RAD;
    gfxText(L[k], CX + cosf(a) * 126, CY + sinf(a) * 126, FontSmall, 0x6E9A90u);
  }
  char b[64];
  if(n.valid)
  {
    const float a = ((n.t.tm_hour + n.t.tm_min / 60.0f) * 15 - 90) * DEG_TO_RAD;
    const int x = CX + cosf(a) * 159, y = CY + sinf(a) * 159;
    canvas.fillSmoothCircle(x, y, 10, 0x071311u);
    canvas.fillSmoothCircle(x, y, 6, 0xFFFFFFu);
    snprintf(b, sizeof(b), "%s %d %s", wd3[uiLang][n.t.tm_wday], n.t.tm_mday, mon3[uiLang][n.t.tm_mon]);
    gfxText(b, CX, 108, FontSmall, C_MUTED);
    snprintf(b, sizeof(b), "%02d:%02d", n.t.tm_hour, n.t.tm_min);
    gfxText(b, CX, 162, FxClockL, C_TEXT);
  }
  else gfxText("--:--", CX, 162, FxClockL, C_MUTED);
  netBadge(84, C_WARN);
  if(n.wx)
  {
    int hiH = -1; float hi = -99;
    for(int i = 0; i < nh && i < 16; i++) if(hs[i].temp > hi) { hi = hs[i].temp; hiH = i; }
    if(hiH >= 0)
    {
      struct tm t; localtime_r(&hs[hiH].t, &t);
      snprintf(b, sizeof(b), T("%d° · max %d° at %dh","%d° · máx %d° às %dh","%d° · máx %d° a las %dh"), (int)lroundf(n.w.temp), (int)lroundf(hi), t.tm_hour);
    }
    else snprintf(b, sizeof(b), "%d°", (int)lroundf(n.w.temp));
    gfxTextFit(b, CX, 210, 230, FontBody, tempColor(n.w.temp));
    int r0 = -1, r1 = -1;
    for(int i = 0; i < nh && i < 16; i++) if(hs[i].prob >= 50) { if(r0 < 0) r0 = i; r1 = i; }
    if(r0 >= 0)
    {
      struct tm a, z; localtime_r(&hs[r0].t, &a); time_t te = hs[r1].t + 3600; localtime_r(&te, &z);
      snprintf(b, sizeof(b), "chuva %dh–%dh", a.tm_hour, z.tm_hour);
      gfxText(b, CX, 236, FontSmall, 0x5AA9FFu);
    }
    else gfxText(weatherConditionShort(n.w.code), CX, 236, FontSmall, C_MUTED);
  }
}

// ---- Minimal ----
static void drawMinimal(const Now &n)
{
  skyDraw(false);
  gfxDim(105);
  char b[32];
  if(n.valid) snprintf(b, sizeof(b), "%02d", n.t.tm_hour); else strlcpy(b, "--", sizeof(b));
  gfxText(b, 124, 128, FxMinimal, 0xFFFFFFu);
  if(n.valid) snprintf(b, sizeof(b), "%02d", n.t.tm_min); else strlcpy(b, "--", sizeof(b));
  gfxText(b, 124, 228, FxMinimal, C_ACCENT);
  if(n.wx)
  {
    weatherIcon(262, 104, 58, n.w.code, n.w.day, 0x101820u);
    snprintf(b, sizeof(b), "%d°", (int)lroundf(n.w.temp));
    gfxText(b, 262, 170, FontTitle, 0xFFFFFFu);
    gfxTextFit(weatherConditionShort(n.w.code), 262, 198, 124, FontSmall, 0xB8C8D4u);
  }
  if(n.valid)
  {
    snprintf(b, sizeof(b), "%s %d", WD3[n.t.tm_wday], n.t.tm_mday);
    gfxText(b, 262, 224, FontSmall, 0x8FA6B4u);
  }
  if(n.wx)
  {
    snprintf(b, sizeof(b), "↑ %d°   ↓ %d°   %d%% chuva", (int)lroundf(n.w.tmax), (int)lroundf(n.w.tmin), n.w.rainProb);
    gfxTextFit(b, CX, 290, 250, FontSmall, 0xB8C8D4u);
  }
  netBadge(52, C_WARN);
}

// ---- Cockpit ----
static void box(int x, int y, const char *label, const char *value, uint32_t col)
{
  const int w = max(gfxTextWidth(value, FxMono20), gfxTextWidth(label, FxMono14)) + 14, h = 40;
  canvas.fillRect(x - w / 2, y - h / 2, w, h, 0x050606u);
  canvas.drawRect(x - w / 2, y - h / 2, w, h, 0x3A4045u);
  gfxText(label, x, y - 9, FxMono14, 0x9AA0A6u);
  gfxText(value, x, y + 9, FxMono20, col);
}

static void drawCockpit(const Now &n)
{
  if(!gfxImage("dial_cockpit"))
  {
    canvas.fillScreen(0x0A0B0Cu);
    for(int i = 0; i < 60; i++)
    {
      const float a = (i * 6 - 90) * DEG_TO_RAD; const bool big = i % 5 == 0;
      canvas.drawWideLine(CX + cosf(a) * (big ? 146 : 152), CY + sinf(a) * (big ? 146 : 152), CX + cosf(a) * 162, CY + sinf(a) * 162, big ? 1.7f : 0.6f, big ? 0xF2F2EEu : 0x6A7075u);
    }
  }
  char b[24];
  const time_t now = time(nullptr); struct tm u; gmtime_r(&now, &u);
  if(n.valid) snprintf(b, sizeof(b), "%02d:%02d", u.tm_hour, u.tm_min); else strlcpy(b, "--:--", sizeof(b));
  box(CX, 104, "UTC", b, 0x3FD0FFu);
  if(n.wx)
  {
    snprintf(b, sizeof(b), "%+d°", (int)lroundf(n.w.temp)); box(118, 216, "OAT", b, 0xFFB020u);
    snprintf(b, sizeof(b), "%03d/%02d", ((n.w.windDir + 5) / 10 * 10) % 360, (int)lroundf(n.w.wind / 1.852f)); box(242, 216, "VENTO", b, 0x6CFF8Au);
    snprintf(b, sizeof(b), "%d", (int)lroundf(n.w.pressure)); box(CX, 262, "QNH", b, 0xF2F2EEu);
  }
  netBadge(140, 0xFFB020u);
  if(!n.valid) return;
  const float s = n.t.tm_sec, m = n.t.tm_min + s / 60.0f, h = (n.t.tm_hour % 12) + m / 60.0f;
  hand(h * 30, 86, 12, 4.2f, 3.2f, 0xF2F2EEu, 0x000000u);
  hand(m * 6, 128, 14, 3.0f, 2.0f, 0xF2F2EEu, 0x000000u);
  hand(s * 6, 142, 26, 1.2f, 1.2f, 0xFFB020u, 0xFFFFFFFF);
  canvas.fillSmoothCircle(CX, CY, 6, 0xFFB020u);
}

// ================= Vintage =================
static void breguet(float deg, float len, float ring, uint32_t col)
{
  const float a = (deg - 90) * DEG_TO_RAD;
  canvas.drawWideLine(CX, CY, CX + cosf(a) * len, CY + sinf(a) * len, 1.3f, col);
  const float rx = CX + cosf(a) * ring, ry = CY + sinf(a) * ring;
  gfxArc(rx, ry, 5.2f, 8.0f, 0, 359.99f, col, 255);
  canvas.drawWedgeLine(CX + cosf(a) * (ring + 7), CY + sinf(a) * (ring + 7), CX + cosf(a) * len, CY + sinf(a) * len, 2.2f, 0.4f, col);
}

static void drawPocket(const Now &n)
{
  if(!gfxImage("dial_pocket")) canvas.fillScreen(0xF1E6CCu);
  const uint32_t INK = 0x2A2218u, STEEL = 0x22408Au;
  char b[48];
  if(n.wx) { snprintf(b, sizeof(b), "%d° · %s", (int)lroundf(n.w.temp), weatherConditionShort(n.w.code)); for(char *p = b; *p; p++) if(*p >= 'A' && *p <= 'Z') *p += 32; gfxTextFit(b, CX, 140, 160, FxFellIt18, 0x6B5530u); }
  netBadge(160, 0xA0522Du);
  if(!n.valid) return;
  const float s = n.t.tm_sec, m = n.t.tm_min + s / 60.0f, h = (n.t.tm_hour % 12) + m / 60.0f;
  const float sa = (s * 6 - 90) * DEG_TO_RAD;
  canvas.drawWideLine(CX - cosf(sa) * 6, 248 - sinf(sa) * 6, CX + cosf(sa) * 25, 248 + sinf(sa) * 25, 0.7f, INK);
  canvas.fillSmoothCircle(CX, 248, 2, INK);
  breguet(h * 30, 82, 54, STEEL);
  breguet(m * 6, 128, 98, STEEL);
  canvas.fillSmoothCircle(CX, CY, 5, STEEL);
}

// ================= Painel de aviao =================
static void drawAltimeter(const Now &n)
{
  if(!gfxImage("dial_alt")) canvas.fillScreen(0x08090Au);
  static const char *const MON[] = {"JAN", "FEV", "MAR", "ABR", "MAI", "JUN", "JUL", "AGO", "SET", "OUT", "NOV", "DEZ"};
  char b[16];
  if(n.valid) { snprintf(b, sizeof(b), "%02d %s", n.t.tm_mday, MON[n.t.tm_mon]); gfxText(b, 241, 180, FxMono14, 0xE8E4D0u); }
  netBadge(140, 0xFFB020u);
  if(!n.valid) return;
  const float s = n.t.tm_sec, m = n.t.tm_min + s / 60.0f, h = (n.t.tm_hour % 12) + m / 60.0f;
  const float ha = (h * 30 - 90) * DEG_TO_RAD, na = ha + PI / 2;
  canvas.fillTriangle(CX + cosf(ha) * 70, CY + sinf(ha) * 70, CX + cosf(na) * 11, CY + sinf(na) * 11, CX - cosf(na) * 11, CY - sinf(na) * 11, 0xE8E4D0u);
  hand(m * 6, 118, 18, 1.8f, 1.6f, 0xE8E4D0u, 0x000000u);
  canvas.fillSmoothCircle(CX, CY, 7, 0x2A2D30u);
  gfxArc(CX, CY, 6, 7.4f, 0, 359.99f, 0xE8E4D0u, 255);
}

static void drawAttitude(const Now &n)
{
  if(!gfxImage("bezel_av")) canvas.fillScreen(0x141618u);
  const float t = millis() / 1000.0f;
  const float roll = 11 * sinf(t * 2 * PI / 9.0f) * DEG_TO_RAD, pitch = 9 * sinf(t * 2 * PI / 13.0f);
  const float cr = cosf(roll), sr = sinf(roll), R = 138;
  uint16_t *buf = (uint16_t *)canvas.getBuffer();
  for(int y = CY - R; y <= CY + R; y++)
    for(int x = CX - R; x <= CX + R; x++)
    {
      const float dx = x - CX, dy = y - CY, d2 = dx * dx + dy * dy;
      if(d2 > (R + 1) * (R + 1)) continue;
      const float cov = constrain(R - sqrtf(d2) + 0.5f, 0.0f, 1.0f);
      const float h = (dy - pitch) * cr - dx * sr;   // distancia ao horizonte (positivo = terra)
      uint32_t c;
      if(h < 0) c = mixc(0x5BA8E8u, 0x2E7FD0u, -h / 140.0f);
      else c = mixc(0x8A5A30u, 0x5E3A1Cu, h / 140.0f);
      uint16_t &p = buf[y * LCD_W + x];
      p = gfxBlend(p, c, (uint8_t)(cov * 255));
    }
  // horizonte e escada de arfagem (giram juntos)
  auto L = [&](float off, float half, float w) {
    const float x0 = -half, x1 = half, y = off + pitch;
    canvas.drawWideLine(CX + x0 * cr - y * sr, CY + x0 * sr + y * cr, CX + x1 * cr - y * sr, CY + x1 * sr + y * cr, w, 0xFFFFFFu);
  };
  L(0, 132, 1.2f);
  L(-22, 26, 0.8f); L(-44, 42, 0.8f); L(22, 26, 0.8f); L(44, 42, 0.8f);
  gfxArc(CX, CY, 138, 145, 0, 359.99f, 0x3A3E42u, 255);
  for(int a : {-60, -45, -30, -20, -10, 0, 10, 20, 30, 45, 60})
  {
    const float r = (a - 90) * DEG_TO_RAD, r0 = (a % 30) ? 130 : 122;
    canvas.drawWideLine(CX + cosf(r) * r0, CY + sinf(r) * r0, CX + cosf(r) * 138, CY + sinf(r) * 138, (a % 30) ? 0.8f : 1.4f, 0xFFFFFFu);
  }
  canvas.fillTriangle(CX, 44, CX - 8, 30, CX + 8, 30, 0xFFB020u);
  // aviao (fixo)
  canvas.drawWideLine(108, CY + 6, 154, CY + 6, 2.6f, 0xFFB020u); canvas.drawWideLine(154, CY + 6, 164, CY + 16, 2.6f, 0xFFB020u);
  canvas.drawWideLine(252, CY + 6, 206, CY + 6, 2.6f, 0xFFB020u); canvas.drawWideLine(206, CY + 6, 196, CY + 16, 2.6f, 0xFFB020u);
  canvas.fillSmoothCircle(CX, CY + 6, 4, 0xFFB020u);
  char b[32];
  if(n.valid) snprintf(b, sizeof(b), "%02d:%02d", n.t.tm_hour, n.t.tm_min); else strlcpy(b, "--:--", sizeof(b));
  const int w = gfxTextWidth(b, FxMono30) + 20;
  gfxBlendRoundRect(CX - w / 2, 106, w, 40, 4, 0x000000u, 140, 0xFFFFFFu, 220);
  gfxText(b, CX, 127, FxMono30, 0xFFFFFFu);
  if(n.wx)
  {
    snprintf(b, sizeof(b), "%d° · VENTO %03d/%02d", (int)lroundf(n.w.temp), ((n.w.windDir + 5) / 10 * 10) % 360, (int)lroundf(n.w.wind / 1.852f));
    gfxTextShadow(b, CX, 252, FxMono14, 0xFFFFFFu);
  }
  netBadge(280, 0xFFB020u);
}

static void drawHsi(const Now &n)
{
  if(!gfxImage("bezel_av")) canvas.fillScreen(0x141618u);
  canvas.fillSmoothCircle(CX, CY, 142, 0x08090Au);
  gfxArc(CX, CY, 140, 144, 0, 359.99f, 0x3A3E42u, 255);
  const float m = n.valid ? n.t.tm_min + n.t.tm_sec / 60.0f : 0;
  size_t len = 0;
  const uint8_t *rose = packFind("rose_280", &len);
  if(rose && len == 280 * 280 / 2) gfxMaskRotated(rose, 280, CX, CY, -m * 6, 0xE8E4D0u);
  canvas.fillTriangle(CX, 46, CX - 8, 32, CX + 8, 32, 0xFFB020u);
  static const int16_t PL[][2] = {{180,120},{188,172},{222,186},{222,194},{188,188},{184,222},{196,232},{196,238},{180,234},{164,238},{164,232},{176,222},{172,188},{138,194},{138,186},{172,172},{180,120}};
  for(int i = 0; i + 1 < (int)(sizeof(PL) / sizeof(PL[0])); i++) canvas.drawWideLine(PL[i][0], PL[i][1], PL[i + 1][0], PL[i + 1][1], 1.1f, 0xFFB020u);
  char b[16];
  snprintf(b, sizeof(b), "%03d", ((int)lroundf(m * 6)) % 360);
  const int w = gfxTextWidth(b, FxMono20) + 14;
  canvas.fillRect(CX - w / 2, 64, w, 24, 0x000000u); canvas.drawRect(CX - w / 2, 64, w, 24, 0xE8E4D0u);
  gfxText(b, CX, 76, FxMono20, 0xE8E4D0u);
  if(n.valid) snprintf(b, sizeof(b), "%02d:%02d", n.t.tm_hour, n.t.tm_min); else strlcpy(b, "--:--", sizeof(b));
  const int w2 = gfxTextWidth(b, FxMono30) + 18;
  gfxBlendRoundRect(CX - w2 / 2, 250, w2, 38, 6, 0x000000u, 200);
  gfxText(b, CX, 270, FxMono30, 0x3FD0FFu);
  netBadge(300, 0xFFB020u);
}

// ---- Simples (como a primeira versao) ----
static void drawSimple(const Now &n)
{
  gfxBackground();
  for(int i = 0; i < 60; i++)
  {
    const float a = (i * 6 - 90) * DEG_TO_RAD;
    const bool big = i % 5 == 0;
    const bool lit = n.valid && cfg.showSeconds && i <= n.t.tm_sec;
    canvas.fillSmoothCircle(CX + cosf(a) * 163, CY + sinf(a) * 163, big ? 3 : 2, lit ? C_ACCENT : (big ? C_RING2 : C_RING));
  }
  if(netConnected()) gfxWifiBars(CX - 14, 78, netRssi(), C_ACCENT, C_RING);
  else gfxText(netApActive() ? T("Setup mode","Modo configuração","Modo configuración") : T("No network","Sem rede","Sin red"), CX, 70, FontSmall, C_WARN);
  char buf[48];
  if(n.valid) snprintf(buf, sizeof(buf), "%02d:%02d", n.t.tm_hour, n.t.tm_min); else strlcpy(buf, "--:--", sizeof(buf));
  gfxText(buf, CX, 158, FontClock, C_TEXT);
  if(n.valid)
  {
    if(n.wx) snprintf(buf, sizeof(buf), "%s  ·  %d°", WEEKDAYS[uiLang][n.t.tm_wday], (int)lroundf(n.w.temp));
    else strlcpy(buf, WEEKDAYS[uiLang][n.t.tm_wday], sizeof(buf));
    gfxText(buf, CX, 238, FontBody, C_MUTED);
    snprintf(buf, sizeof(buf), T("%d %s","%d de %s","%d de %s"), n.t.tm_mday, MONTHS[uiLang][n.t.tm_mon]);
    gfxText(buf, CX, 268, FontBody, C_TEXT);
  }
  else gfxText(T("Waiting for time…","Aguardando a hora…","Esperando la hora…"), CX, 250, FontBody, C_MUTED);
}

void clockDraw()
{
  const Now n = snap();
  switch(feat.cStyle)
  {
    case CLK_CLASSIC: drawClassic(n); break;
    case CLK_RING: drawRing(n); break;
    case CLK_MINIMAL: drawMinimal(n); break;
    case CLK_COCKPIT: drawCockpit(n); break;
    case CLK_SIMPLE: drawSimple(n); break;
    case CLK_POCKET: drawPocket(n); break;
    case CLK_ALTIMETER: drawAltimeter(n); break;
    case CLK_ATTITUDE: drawAttitude(n); break;
    case CLK_HSI: drawHsi(n); break;
    default: drawReal(n); break;
  }
}

uint32_t clockFrameMs()
{
  switch(feat.cStyle)
  {
    case CLK_REAL: return skyFrameMs();
    case CLK_ATTITUDE: return 80;    // horizonte balancando
    case CLK_HSI: return 250;        // rosa girando com os segundos
    default: return 0;
  }
}
