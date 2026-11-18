#include "Photos.h"
#include "Lang.h"
#include "Gfx.h"
#include "Feat.h"
#include "Weather.h"
#include "Net.h"
#include <LittleFS.h>
#include <time.h>
#include <math.h>

static const char *DIR_PATH = "/photos";
#define MAX_PHOTOS 300

static String names[MAX_PHOTOS];
static int count = 0;
static volatile bool rescan = true;
static int cur = -1;
static uint32_t shownAt = 0;
static bool paused = false;
static LGFX_Sprite *imgA = nullptr, *imgB = nullptr;   // atual e proxima
static uint32_t fadeStart = 0;
static bool fading = false;
static uint32_t pauseMsgUntil = 0;

static void scan()
{
  rescan = false;
  count = 0;
  if(!LittleFS.exists(DIR_PATH)) LittleFS.mkdir(DIR_PATH);
  File dir = LittleFS.open(DIR_PATH);
  if(!dir) return;
  for(File f = dir.openNextFile(); f && count < MAX_PHOTOS; f = dir.openNextFile())
  {
    String n = f.name();
    if(n.endsWith(".jpg") || n.endsWith(".jpeg")) names[count++] = n;
    f.close();
  }
  dir.close();
  std::sort(names, names + count);
  if(cur >= count) cur = -1;
}

static LGFX_Sprite *makeSprite()
{
  auto *s = new LGFX_Sprite();
  s->setPsram(true); s->setColorDepth(16);
  if(!s->createSprite(LCD_W, LCD_H)) { delete s; return nullptr; }
  return s;
}

static bool load(LGFX_Sprite *dst, int i)
{
  if(i < 0 || i >= count) return false;
  dst->fillScreen(TFT_BLACK);
  String path = String(DIR_PATH) + "/" + names[i];
  File f = LittleFS.open(path, "r");
  if(!f) return false;
  const size_t sz = f.size();
  uint8_t *buf = sz && sz < 1024 * 1024 ? (uint8_t *)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM) : nullptr;
  if(!buf) { f.close(); return false; }
  const size_t got = f.read(buf, sz);
  f.close();
  const bool ok = got == sz && dst->drawJpg(buf, sz, 0, 0, LCD_W, LCD_H);
  heap_caps_free(buf);
  return ok;
}

static int stepReq = 0;   // pedido do controle remoto: -1 / +1

static int nextIndex()
{
  if(stepReq) { const int d = stepReq; stepReq = 0; return count ? ((cur + d) % count + count) % count : -1; }
  if(count <= 1) return count ? 0 : -1;
  if(feat.pShuffle) { int n; do { n = esp_random() % count; } while(n == cur); return n; }
  return (cur + 1) % count;
}

void photosBegin() { rescan = true; }
bool photosStep(int d) { if(count < 2) return false; stepReq = d; return true; }
void photosRescan() { rescan = true; }
int photosCount() { return count; }   // a lista so e relida na tarefa da tela

void photosOpen()
{
  if(rescan) scan();
  shownAt = 0;   // troca na hora
}

bool photosAnimated() { return fading || (!paused && count > 1) || pauseMsgUntil; }

void photosLongPress()
{
  paused = !paused;
  pauseMsgUntil = millis() + 1500;
  shownAt = millis();
}

// ---- Relogios sobre a foto ----
static const char *const WD3[] = {"dom", "seg", "ter", "qua", "qui", "sex", "sáb"};


// Disco de vidro: desfoque da foto calculado uma vez por foto

static void wxLine(char *b, size_t n, bool withDay, const struct tm &tm)
{
  float t; int code; bool day;
  if(weatherSummary(&t, &code, &day))
  {
    if(withDay) snprintf(b, n, "%d° · %s · %s %d", (int)lroundf(t), weatherConditionShort(code), WD3[tm.tm_wday], tm.tm_mday);
    else snprintf(b, n, "%d° · %s", (int)lroundf(t), weatherConditionShort(code));
  }
  else if(withDay) snprintf(b, n, "%s %d", WD3[tm.tm_wday], tm.tm_mday);
  else b[0] = 0;
}

static void clockOverlay(bool fadingNow)
{
  if(feat.pStyle == PCLK_NONE || time(nullptr) < 1700000000) return;
  time_t t = time(nullptr); struct tm tm; localtime_r(&t, &tm);
  char hm[8], b[80];
  snprintf(hm, sizeof(hm), "%02d:%02d", tm.tm_hour, tm.tm_min);
  switch(feat.pStyle)
  {
    case PCLK_FOOTER:
      gfxShadeRows(206, LCD_H, 0x000000u, 0, 175);
      gfxTextShadow(hm, CX, 268, FxClockL, 0xFFFFFFu);
      wxLine(b, sizeof(b), true, tm);
      gfxTextShadow(b, CX, 310, FontSmall, 0xE8EEF2u);
      break;
    case PCLK_HANDS:
    {
      for(int i = 0; i < 12; i++)
      {
        const float a = (i * 30 - 90) * DEG_TO_RAD;
        const float r0 = i % 3 ? 166 : 160;
        canvas.drawWideLine(CX + cosf(a) * r0 + 1, CY + sinf(a) * r0 + 2, CX + cosf(a) * 175 + 1, CY + sinf(a) * 175 + 2, i % 3 ? 1.0f : 1.8f, 0x101418u);
        canvas.drawWideLine(CX + cosf(a) * r0, CY + sinf(a) * r0, CX + cosf(a) * 175, CY + sinf(a) * 175, i % 3 ? 1.0f : 1.8f, 0xFFFFFFu);
      }
      const float s = tm.tm_sec, m = tm.tm_min + s / 60.0f, h = (tm.tm_hour % 12) + m / 60.0f;
      auto hand = [](float deg, float len, float r0, float r1, uint32_t col) {
        const float a = (deg - 90) * DEG_TO_RAD;
        canvas.drawWedgeLine(CX + 2, CY + 3, CX + cosf(a) * len + 2, CY + sinf(a) * len + 3, r0, r1, 0x0C1014u);
        canvas.drawWedgeLine(CX, CY, CX + cosf(a) * len, CY + sinf(a) * len, r0, r1, col);
      };
      hand(h * 30, 84, 5.0f, 2.0f, 0xFFFFFFu);
      hand(m * 6, 128, 3.6f, 1.2f, 0xFFFFFFu);
      canvas.fillSmoothCircle(CX, CY, 6, 0xFFFFFFu);
      wxLine(b, sizeof(b), true, tm);
      const int w = gfxTextWidth(b, FontSmall) + 26;
      gfxBlendRoundRect(CX - w / 2, 288, w, 28, 14, 0x000000u, 110);
      gfxText(b, CX, 302, FontSmall, 0xFFFFFFu);
      break;
    }
    default: break;
  }
}

// Aviso do clima: aparece quando muda e a cada 10 min, por alguns segundos
static char popLast[40] = "";
static uint32_t popUntil = 0, popNext = 0;
static void weatherPop()
{
  if(!feat.pWxPop) return;
  char t1[40], t2[48];
  if(!weatherAlert(t1, sizeof(t1), t2, sizeof(t2))) { popLast[0] = 0; popUntil = 0; return; }
  const uint32_t now = millis();
  if(strcmp(t1, popLast)) { strlcpy(popLast, t1, sizeof(popLast)); popUntil = now + 12000; popNext = now + 600000; }
  else if((int32_t)(now - popNext) >= 0) { popUntil = now + 9000; popNext = now + 600000; }
  if(!popUntil || (int32_t)(now - popUntil) >= 0) return;
  const int cy = (feat.pStyle == PCLK_HANDS) ? CY : 148;
  const int w = 284, h = 66, x = CX - w / 2, y = cy - h / 2;
  gfxBlendRoundRect(x, y, w, h, 22, 0x121A26u, 200, 0xFFFFFFu, 55);
  float temp; int code = 61; bool day = true; weatherSummary(&temp, &code, &day);
  const int ic = strstr(t1, "Trovo") ? 95 : 63;
  weatherIcon(x + 36, y + h / 2 - 2, 42, ic, day, 0x121A26u);
  gfxTextFit(t1, x + 66, y + 22, w - 78, FontBody, 0xFFFFFFu, middle_left);
  gfxTextFit(t2, x + 66, y + 46, w - 78, FontSmall, 0xB8C6D6u, middle_left);
}

static bool popVisible()
{
  return feat.pWxPop && popUntil && (int32_t)(millis() - popUntil) < 0 && popLast[0];
}

static void overlay(bool fadingNow)
{
  // com o aviso na tela, os relogios do centro saem por alguns segundos
  if(!(popVisible() && (feat.pStyle == PCLK_HANDS))) clockOverlay(fadingNow);
  weatherPop();
  if(pauseMsgUntil)
  {
    if((int32_t)(millis() - pauseMsgUntil) >= 0) pauseMsgUntil = 0;
    else
    {
      gfxBlendRoundRect(CX - 70, 28, 140, 34, 17, 0x000000u, 150);
      gfxText(paused ? "Pausado" : "Continuando", CX, 45, FontSmall, 0xFFFFFFu);
    }
  }
}

void photosDraw()
{
  if(rescan) scan();
  if(!count)
  {
    gfxBackground();
    gfxText(T("Photo frame","Porta-retrato","Marco de fotos"), CX, 120, FontTitle, C_ACCENT);
    gfxWrapRound(T("Send photos from the web panel in Features → Photo frame","Envie fotos pelo painel web em Recursos → Porta-retrato","Envía fotos desde el panel web en Funciones → Marco de fotos"), 160, FontBody, C_MUTED, 28, 4, 40);
    return;
  }
  if(!imgA) imgA = makeSprite();
  if(!imgB) imgB = makeSprite();
  if(!imgA || !imgB) { gfxBackground(); gfxStatusLine("Sem memória para as fotos", C_BAD); return; }

  const uint32_t now = millis();
  if(cur < 0) { cur = feat.pShuffle ? esp_random() % count : 0; load(imgA, cur); shownAt = now; }
  else if(!fading && count > 1 && (stepReq || (!paused && (shownAt == 0 || now - shownAt >= feat.pSecs * 1000UL))))
  {
    const int n = nextIndex();
    if(load(imgB, n))
    {
      cur = n;
      if(feat.pFade) { fading = true; fadeStart = now; }
      else std::swap(imgA, imgB);
    }
    shownAt = now;
  }

  if(fading)
  {
    const uint32_t FADE_MS = 700;
    const uint32_t el = now - fadeStart;
    if(el >= FADE_MS) { fading = false; std::swap(imgA, imgB); memcpy(canvas.getBuffer(), imgA->getBuffer(), LCD_W * LCD_H * 2); }
    else
    {
      const int a = el * 256 / FADE_MS;
      const uint16_t *pa = (const uint16_t *)imgA->getBuffer(), *pb = (const uint16_t *)imgB->getBuffer();
      uint16_t *out = (uint16_t *)canvas.getBuffer();
      for(int i = 0; i < LCD_W * LCD_H; i++)
      {
        const uint16_t x = (uint16_t)(pa[i] >> 8 | pa[i] << 8), y = (uint16_t)(pb[i] >> 8 | pb[i] << 8);
        const int r = ((x >> 11) * (256 - a) + (y >> 11) * a) >> 8;
        const int g = (((x >> 5) & 63) * (256 - a) + ((y >> 5) & 63) * a) >> 8;
        const int b = ((x & 31) * (256 - a) + (y & 31) * a) >> 8;
        const uint16_t v = (uint16_t)((r << 11) | (g << 5) | b);
        out[i] = (uint16_t)(v >> 8 | v << 8);
      }
    }
  }
  else memcpy(canvas.getBuffer(), imgA->getBuffer(), LCD_W * LCD_H * 2);
  overlay(fading);
}
