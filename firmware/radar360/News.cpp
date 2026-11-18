#include "News.h"
#include "Lang.h"
#include "Gfx.h"
#include "Feat.h"
#include "Http.h"
#include "Net.h"
#include "Ui.h"
#include "Crash.h"
#include <esp_heap_caps.h>
#include <time.h>

#define MAX_ITEMS 40

struct Item { char title[220]; char source[48]; char link[400]; time_t date; uint8_t topic; };

static SemaphoreHandle_t mtx;
static Item *items = nullptr;       // PSRAM
static int itemCount = 0;
static int cur = 0;
static uint32_t shownAt = 0;
static uint32_t fetchAt = 0;
static uint32_t gen = 0;
static char status[64] = "Buscando notícias…";
static bool qr = false;

static void lock() { xSemaphoreTake(mtx, portMAX_DELAY); }
static void unlock() { xSemaphoreGive(mtx); }

static const char *TOPIC_NAMES[] = {"Principais", "Brasil", "Mundo", "Negócios", "Tecnologia", "Entretenimento", "Esportes", "Ciência", "Saúde"};
static const char *TOPIC_IDS[] = {"", "NATION", "WORLD", "BUSINESS", "TECHNOLOGY", "ENTERTAINMENT", "SPORTS", "SCIENCE", "HEALTH"};
const char *newsTopicName(int b) { return b >= 0 && b < 9 ? TOPIC_NAMES[b] : (b == 9 ? "Busca" : "Feed"); }

// ---- XML simples ----

// Aspas e espacos "tipograficos" viram os simples (as fontes do aparelho sao enxutas)
static void normalizePunct(char *s)
{
  unsigned char *w = (unsigned char *)s;
  for(unsigned char *r = (unsigned char *)s; *r;)
  {
    if(r[0] == 0xE2 && r[1] == 0x80 && r[2])
    {
      const unsigned char c = r[2]; char out = 0;
      if(c == 0x98 || c == 0x99 || c == 0x9A || c == 0x9B || c == 0xB2 || c == 0xB9 || c == 0xBA) out = '\'';
      else if(c >= 0x9C && c <= 0x9F) out = '"';
      else if(c == 0xB3) out = '"';
      else if(c == 0x90 || c == 0x91) out = '-';
      else if(c == 0xAF || (c >= 0x80 && c <= 0x8A)) out = ' ';
      else if(c == 0x8B || c == 0x8C || c == 0x8D) { r += 3; continue; }   // espacos de largura zero
      if(out) { *w++ = out; r += 3; continue; }
    }
    if(r[0] == 0xC2 && r[1] == 0xA0) { *w++ = ' '; r += 2; continue; }
    if(r[0] == 0xC2 && (r[1] == 0xAB || r[1] == 0xBB)) { *w++ = '"'; r += 2; continue; }
    *w++ = *r++;
  }
  *w = 0;
}
static void decodeEntities(char *s)
{
  char *w = s;
  for(char *r = s; *r;)
  {
    if(*r == '&')
    {
      char *semi = strchr(r, ';');
      if(semi && semi - r < 10)
      {
        uint32_t cp = 0;
        if(!strncmp(r, "&amp;", 5)) cp = '&'; else if(!strncmp(r, "&lt;", 4)) cp = '<'; else if(!strncmp(r, "&gt;", 4)) cp = '>';
        else if(!strncmp(r, "&quot;", 6)) cp = '"'; else if(!strncmp(r, "&apos;", 6)) cp = '\''; else if(!strncmp(r, "&nbsp;", 6)) cp = ' ';
        else if(r[1] == '#') cp = (r[2] == 'x' || r[2] == 'X') ? strtoul(r + 3, nullptr, 16) : strtoul(r + 2, nullptr, 10);
        if(cp)
        {
          if(cp < 0x80) *w++ = (char)cp;
          else if(cp < 0x800) { *w++ = 0xC0 | (cp >> 6); *w++ = 0x80 | (cp & 0x3F); }
          else if(cp < 0x10000) { *w++ = 0xE0 | (cp >> 12); *w++ = 0x80 | ((cp >> 6) & 0x3F); *w++ = 0x80 | (cp & 0x3F); }
          r = semi + 1;
          continue;
        }
      }
    }
    *w++ = *r++;
  }
  *w = 0;
}

static void stripTags(char *s)
{
  char *w = s; bool in = false;
  for(char *r = s; *r; r++) { if(*r == '<') in = true; else if(*r == '>') { in = false; continue; } if(!in) *w++ = *r; }
  *w = 0;
}

static void squeeze(char *s)
{
  char *w = s; bool sp = true;
  for(char *r = s; *r; r++)
  {
    const bool isSp = *r == ' ' || *r == '\n' || *r == '\r' || *r == '\t';
    if(isSp) { if(!sp) *w++ = ' '; sp = true; } else { *w++ = *r; sp = false; }
  }
  while(w > s && w[-1] == ' ') w--;
  *w = 0;
}

// Copia o conteudo de <tag ...>...</tag> dentro de [p, end)
static bool tagText(const char *p, const char *end, const char *tag, char *out, size_t n)
{
  char open[24]; snprintf(open, sizeof(open), "<%s", tag);
  const char *a = p;
  while((a = strstr(a, open)) && a < end)
  {
    const char c = a[strlen(open)];
    if(c == '>' || c == ' ') break;
    a++;
  }
  if(!a || a >= end) return false;
  const char *gt = strchr(a, '>'); if(!gt || gt >= end) return false;
  if(gt[-1] == '/') { out[0] = 0; return true; }
  char close[24]; snprintf(close, sizeof(close), "</%s>", tag);
  const char *b = strstr(gt, close); if(!b || b > end) return false;
  const char *s = gt + 1;
  if(!strncmp(s, "<![CDATA[", 9)) { s += 9; const char *ce = strstr(s, "]]>"); if(ce && ce < b) b = ce; }
  size_t len = min((size_t)(b - s), n - 1);
  memcpy(out, s, len); out[len] = 0;
  return true;
}

static bool attr(const char *p, const char *end, const char *tag, const char *name, char *out, size_t n)
{
  char open[24]; snprintf(open, sizeof(open), "<%s ", tag);
  const char *a = strstr(p, open); if(!a || a >= end) return false;
  const char *gt = strchr(a, '>'); if(!gt || gt > end) return false;
  char key[24]; snprintf(key, sizeof(key), "%s=\"", name);
  const char *k = strstr(a, key); if(!k || k > gt) return false;
  k += strlen(key);
  const char *q = strchr(k, '"'); if(!q || q > gt) return false;
  size_t len = min((size_t)(q - k), n - 1); memcpy(out, k, len); out[len] = 0;
  return true;
}

static time_t parseDate(const char *s)
{
  // RFC 822: "Fri, 13 Nov 2026 03:10:00 GMT"  |  ISO 8601: "2026-11-13T03:10:00Z"
  static const char *M = "JanFebMarAprMayJunJulAugSepOctNovDec";
  struct tm t; memset(&t, 0, sizeof(t));
  int d, y, hh = 0, mm = 0, ss = 0; char mon[4] = "";
  const char *c = strchr(s, ','); const char *p = c ? c + 1 : s;
  if(sscanf(p, " %d %3s %d %d:%d:%d", &d, mon, &y, &hh, &mm, &ss) >= 3)
  {
    const char *f = strstr(M, mon); if(!f) return 0;
    t.tm_mday = d; t.tm_mon = (f - M) / 3; t.tm_year = y - 1900;
  }
  else if(sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &t.tm_mon, &d, &hh, &mm, &ss) >= 3) { t.tm_year = y - 1900; t.tm_mon -= 1; t.tm_mday = d; }
  else return 0;
  t.tm_hour = hh; t.tm_min = mm; t.tm_sec = ss;
  // timegm
  const int yy = t.tm_year + 1900, mo = t.tm_mon + 1;
  const int a = (14 - mo) / 12, y2 = yy + 4800 - a, m2 = mo + 12 * a - 3;
  const long jdn = t.tm_mday + (153 * m2 + 2) / 5 + 365L * y2 + y2 / 4 - y2 / 100 + y2 / 400 - 32045;
  return (time_t)((jdn - 2440588) * 86400L + hh * 3600L + mm * 60L + ss);
}

static int parseFeed(const char *xml, Item *out, int max, uint8_t topic)
{
  int n = 0;
  const bool atom = strstr(xml, "<entry") && !strstr(xml, "<item");
  const char *openTag = atom ? "<entry" : "<item";
  const char *closeTag = atom ? "</entry>" : "</item>";
  const char *p = xml;
  while(n < max && (p = strstr(p, openTag)))
  {
    const char *end = strstr(p, closeTag); if(!end) break;
    Item &it = out[n]; memset(&it, 0, sizeof(it));
    char tmp[600];
    if(tagText(p, end, "title", tmp, sizeof(tmp))) { decodeEntities(tmp); stripTags(tmp); normalizePunct(tmp); squeeze(tmp); strlcpy(it.title, tmp, sizeof(it.title)); }
    if(atom) attr(p, end, "link", "href", it.link, sizeof(it.link));
    else if(tagText(p, end, "link", tmp, sizeof(tmp))) { squeeze(tmp); decodeEntities(tmp); strlcpy(it.link, tmp, sizeof(it.link)); }
    if(tagText(p, end, "source", tmp, sizeof(tmp))) { decodeEntities(tmp); normalizePunct(tmp); squeeze(tmp); strlcpy(it.source, tmp, sizeof(it.source)); }
    if(tagText(p, end, atom ? "updated" : "pubDate", tmp, sizeof(tmp))) it.date = parseDate(tmp);
    // Google Noticias: "Manchete - Fonte"
    if(it.source[0])
    {
      char suffix[60]; snprintf(suffix, sizeof(suffix), " - %s", it.source);
      const size_t lt = strlen(it.title), ls = strlen(suffix);
      if(lt > ls && !strcmp(it.title + lt - ls, suffix)) it.title[lt - ls] = 0;
    }
    it.topic = topic;
    if(it.title[0]) n++;
    p = end;
    vTaskDelay(1);
  }
  return n;
}

static void urlEncode(const char *in, char *out, size_t n)
{
  size_t o = 0;
  for(const unsigned char *p = (const unsigned char *)in; *p && o + 4 < n; p++)
  {
    if(isalnum(*p) || *p == '-' || *p == '_' || *p == '.') out[o++] = *p;
    else if(*p == ' ') out[o++] = '+';
    else o += snprintf(out + o, n - o, "%%%02X", *p);
  }
  out[o] = 0;
}

static void fetchAll()
{
  static Item *tmp = nullptr;
  if(!tmp) tmp = (Item *)heap_caps_malloc(sizeof(Item) * MAX_ITEMS * 2, MALLOC_CAP_SPIRAM);
  if(!items) items = (Item *)heap_caps_malloc(sizeof(Item) * MAX_ITEMS, MALLOC_CAP_SPIRAM);
  if(!tmp || !items) return;
  const char *GN = "hl=pt-BR&gl=BR&ceid=BR:pt-419";
  struct Src { char url[300]; uint8_t topic; } srcs[11]; int ns = 0;
  for(int b = 0; b < 9; b++)
  {
    if(!(feat.nTopics & (1 << b))) continue;
    if(b == 0) snprintf(srcs[ns].url, sizeof(srcs[ns].url), "https://news.google.com/rss?%s", GN);
    else snprintf(srcs[ns].url, sizeof(srcs[ns].url), "https://news.google.com/rss/headlines/section/topic/%s?%s", TOPIC_IDS[b], GN);
    srcs[ns++].topic = b;
  }
  if(feat.nQuery[0])
  {
    char q[150]; urlEncode(feat.nQuery, q, sizeof(q));
    snprintf(srcs[ns].url, sizeof(srcs[ns].url), "https://news.google.com/rss/search?q=%s&%s", q, GN);
    srcs[ns++].topic = 9;
  }
  if(feat.nUrl[0]) { strlcpy(srcs[ns].url, feat.nUrl, sizeof(srcs[ns].url)); srcs[ns++].topic = 10; }
  if(!ns) { lock(); strlcpy(status, T("Pick topics in the web panel","Escolha temas no painel web","Elige temas en el panel web"), sizeof(status)); itemCount = 0; unlock(); return; }

  const int perFeed = max(4, (MAX_ITEMS * 2) / ns);
  int total = 0;
  HttpBuf buf; char err[48]; char lastErr[64] = "";
  for(int i = 0; i < ns && total < MAX_ITEMS * 2; i++)
  {
    if(httpGet(srcs[i].url, buf, 400 * 1024, err, sizeof(err), "application/rss+xml, application/atom+xml, text/xml", true) == 200)
      total += parseFeed(buf.str(), tmp + total, min(perFeed, MAX_ITEMS * 2 - total), srcs[i].topic);
    else snprintf(lastErr, sizeof(lastErr), T("News: %s","Notícias: %s","Noticias: %s"), err);
    vTaskDelay(1);
  }
  buf.release();
  // Remove repetidas e ordena da mais nova para a mais antiga
  int n = 0;
  for(int i = 0; i < total; i++)
  {
    bool dup = false;
    for(int k = 0; k < n; k++) if(!strcmp(tmp[k].title, tmp[i].title)) { dup = true; break; }
    if(!dup) tmp[n++] = tmp[i];
  }
  std::sort(tmp, tmp + n, [](const Item &a, const Item &b) { return a.date > b.date; });
  n = min(n, MAX_ITEMS);
  lock();
  memcpy(items, tmp, sizeof(Item) * n);
  itemCount = n;
  if(cur >= n) cur = 0;
  if(n) status[0] = 0; else strlcpy(status, lastErr[0] ? lastErr : T("No news","Nenhuma notícia","Sin noticias"), sizeof(status));
  unlock();
}

void newsBegin() { mtx = xSemaphoreCreateMutex(); }

void newsWork()
{
  if(!feat.scrNews || !netConnected() || !netTimeValid()) return;
  if(gen != featGen || !fetchAt || (int32_t)(millis() - fetchAt) >= 0)
  {
    const bool changed = gen != featGen;
    gen = featGen;
    fetchAt = millis() + feat.nRefreshMin * 60000UL;
    if(changed || uiCurrent() == SCR_NEWS || !itemCount) { crumbWorker("noticias"); fetchAll(); }
    else fetchAt = millis() + 60000;   // sem pressa: busca quando a tela for aberta
  }
}

void newsOpen() { shownAt = millis(); qr = false; if(!itemCount) fetchAt = 0; }
int newsCount() { return itemCount; }

bool newsTick()
{
  if(qr || itemCount < 2) return false;
  if(millis() - shownAt >= feat.nSecs * 1000UL) { shownAt = millis(); cur = (cur + 1) % itemCount; return true; }
  return false;
}

void newsLongPress() { if(itemCount) qr = !qr; shownAt = millis(); }
bool newsShortPress() { if(!qr) return false; qr = false; return true; }
bool newsStep(int d)
{
  if(!itemCount) return false;
  qr = false;
  cur = ((cur + d) % itemCount + itemCount) % itemCount;
  shownAt = millis();
  return true;
}

// ---- Tema Aurora (jornal classico) ----
static void upperUtf8(char *b)
{
  for(unsigned char *p = (unsigned char *)b; *p; p++)
  {
    if(*p >= 'a' && *p <= 'z') *p -= 32;
    else if(*p == 0xC3 && p[1] >= 0xA0 && p[1] <= 0xBE && p[1] != 0xB7) { p[1] -= 0x20; p++; }
  }
}

static void ornament(int y, uint32_t col)
{
  gfxBlendLine(CX - 64, y, CX - 10, y, col, 200);
  gfxBlendLine(CX + 10, y, CX + 64, y, col, 200);
  canvas.fillTriangle(CX, y - 5, CX + 5, y, CX, y + 5, col);
  canvas.fillTriangle(CX, y - 5, CX - 5, y, CX, y + 5, col);
}

static void drawAurora(const Item &it, int idx, int total)
{
  const uint32_t GOLD = 0xF2C66Du, LAV = 0xB9B4DEu;
  gfxArc(CX, CY, 168, 169.4f, 0, 359.99f, GOLD, 80);
  // quanto falta para a proxima manchete
  const float frac = min(1.0f, (millis() - shownAt) / (feat.nSecs * 1000.0f));
  gfxArc(CX, CY, 161, 163, 0, 359.99f, GOLD, 35);
  if(frac > 0.01f) gfxArc(CX, CY, 161, 163, 0, frac * 360, GOLD, 200);

  char b[96], ago[24];
  strlcpy(b, newsTopicName(it.topic), sizeof(b)); upperUtf8(b);
  gfxTextSpaced(b, CX, 62, FxCinzel14, GOLD, 4);
  ornament(84, GOLD);

  int need = 0;
  gfxWrapRoundCentered(it.title, 176, FxSerif30, 0xFFFFFFu, 33, 5, 36, false, &need);
  if(need <= 5) gfxWrapRoundCentered(it.title, 176, FxSerif30, 0xFFFFFFu, 33, 5, 36);
  else gfxWrapRoundCentered(it.title, 176, FxSerif22, 0xFFFFFFu, 25, 6, 32);
  ornament(266, GOLD);
  snprintf(b, sizeof(b), "%s · %s", it.source[0] ? it.source : T("Google News","Google Notícias","Google Noticias"), gfxAgo(it.date, ago, sizeof(ago)));
  gfxTextFit(b, CX, 288, 220, FontSmall, LAV);
  char r1[16], r2[16];
  snprintf(b, sizeof(b), "%s de %s", gfxRoman(idx + 1, r1, sizeof(r1)), gfxRoman(total, r2, sizeof(r2)));
  gfxText(b, CX, 316, FxSerif22, GOLD);
}


// ======================= Estilo TV =======================
static uint32_t mix888(uint32_t a, uint32_t b, float t)
{
  t = constrain(t, 0.0f, 1.0f);
  const int r = ((a >> 16) & 255) + (((int)((b >> 16) & 255) - (int)((a >> 16) & 255)) * t);
  const int g = ((a >> 8) & 255) + (((int)((b >> 8) & 255) - (int)((a >> 8) & 255)) * t);
  const int bl = (a & 255) + (((int)(b & 255) - (int)(a & 255)) * t);
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}
// 2 Telejornal (GC), 3 Plantao urgente, 4 Canal 24 h, 5 Teletexto. Marca inventada "R360".
static bool isUrgent(const char *t)
{
  static const char *const K[] = {"urgente", "alerta", "defesa civil", "plantão", "plantao", "ao vivo", "última hora", "ultima hora",
                                  "terremoto", "tsunami", "explosão", "explosao", "evacua", "tempestade severa"};
  char low[220]; strlcpy(low, t, sizeof(low));
  for(unsigned char *p = (unsigned char *)low; *p; p++)
  {
    if(*p >= 'A' && *p <= 'Z') *p += 32;
    else if(*p == 0xC3 && p[1] >= 0x80 && p[1] <= 0x9E) { p[1] += 0x20; p++; }
  }
  for(const char *k : K) if(strstr(low, k)) return true;
  return false;
}

// Quebra de linha num retangulo (alinhado a esquerda); a ultima linha ganha "…" se faltar espaco
static int wrapBox(const char *s, int x, int y, int w, const uint8_t *font, uint32_t color, int lineH, int maxLines, bool draw = true)
{
  gfxFont(font);
  canvas.setTextDatum(top_left);
  canvas.setTextColor(color);
  char line[160]; int lines = 0;
  const char *p = s;
  while(*p && lines < maxLines)
  {
    while(*p == ' ') p++;
    if(!*p) break;
    size_t best = 0; const char *q = p;
    while(true)
    {
      const char *e = q; while(*e && *e != ' ') e++;
      const size_t len = e - p;
      if(len >= sizeof(line) - 4) break;
      memcpy(line, p, len); line[len] = 0;
      if(canvas.textWidth(line) > w) break;
      best = len; if(!*e) break; q = e + 1;
    }
    if(!best)   // palavra maior que a linha: corta
    {
      size_t len = 0; while(p[len] && len < sizeof(line) - 4) { memcpy(line, p, len + 1); line[len + 1] = 0; if(canvas.textWidth(line) > w) break; len++; }
      best = max((size_t)1, len);
    }
    memcpy(line, p, best); line[best] = 0;
    p += best;
    if(lines == maxLines - 1 && *p)
    {
      // ultima linha com reticencias
      size_t L = strlen(line);
      while(L && canvas.textWidth((String(line) + "…").c_str()) > w) { do { L--; } while(L && ((unsigned char)line[L] & 0xC0) == 0x80); line[L] = 0; }
      strlcat(line, "…", sizeof(line));
    }
    if(draw) canvas.drawString(line, x, y + lines * lineH);
    lines++;
  }
  return lines;
}

// Barra de manchetes correndo (letreiro)
static void ticker(int y, int h, uint32_t bg, uint32_t fg, const char *label, uint32_t lbg, uint32_t lfg, const char *text, float speed)
{
  canvas.fillRect(0, y, LCD_W, h, bg);
  const int hw = gfxRoundHalfWidth(y + h / 2, 0);
  int xs = CX - hw + 6;
  gfxFont(FxBarlow17);
  if(label && label[0])
  {
    const int lw = canvas.textWidth(label) + 14;
    canvas.fillRect(xs, y + 3, lw, h - 6, lbg);
    gfxText(label, xs + lw / 2, y + h / 2, FxBarlow17, lfg);
    xs += lw + 6;
  }
  gfxFont(FxBarlow17);
  const int tw = canvas.textWidth(text) + 50;
  const float off = fmodf(millis() / 1000.0f * speed, (float)tw);
  canvas.setClipRect(xs, y, LCD_W - xs, h);
  canvas.setTextDatum(middle_left);
  canvas.setTextColor(fg);
  for(int k = 0; k < 2; k++) canvas.drawString(text, xs - (int)off + k * tw, y + h / 2 + 1);
  canvas.clearClipRect();
}

static void tickerText(char *out, size_t n, int idx, int total)
{
  out[0] = 0;
  lock();
  for(int k = 1; k <= 6 && k < total; k++)
  {
    const Item &o = items[(idx + k) % total];
    if(out[0]) strlcat(out, "   •   ", n);
    strlcat(out, o.title, n);
  }
  unlock();
}

static void upperUtf8b(char *b)
{
  for(unsigned char *p = (unsigned char *)b; *p; p++)
  {
    if(*p >= 'a' && *p <= 'z') *p -= 32;
    else if(*p == 0xC3 && p[1] >= 0xA0 && p[1] <= 0xBE && p[1] != 0xB7) { p[1] -= 0x20; p++; }
  }
}

static void clockStr(char *b, size_t n)
{
  const time_t t = time(nullptr); struct tm tm; localtime_r(&t, &tm);
  snprintf(b, n, "%02d:%02d", tm.tm_hour, tm.tm_min);
}

static void drawGC(const Item &it, int idx, int total)
{
  if(!gfxImage("bg_studio")) gfxGradientV(0x173470u, 0x0B1838u, 0x050A18u);
  char b[96], hm[8]; clockStr(hm, sizeof(hm));
  canvas.fillRect(62, 38, 96, 22, 0xE53935u);
  gfxText("R360 NEWS", 110, 49, FxOsw14, 0xFFFFFFu);
  gfxBlendRect(216, 38, 82, 22, 0x000000u, 140);
  const bool blink = (millis() / 600) & 1;
  canvas.fillSmoothCircle(229, 49, 4, blink ? 0xFF3B3Bu : 0x7A1C1Cu);
  gfxText("AO VIVO", 262, 49, FxOsw14, 0xFFFFFFu);
  // tarja
  strlcpy(b, newsTopicName(it.topic), sizeof(b)); upperUtf8b(b);
  canvas.fillRect(14, 192, 332, 24, 0xE53935u);
  gfxText(b, 30, 204, FxOsw14, 0xFFFFFFu, middle_left);
  for(int y = 216; y < 294; y++) canvas.drawFastHLine(14, y, 332, mix888(0xFFFFFFu, 0xE3E8F0u, (y - 216) / 78.0f));
  wrapBox(it.title, 30, 221, 300, FxHead18, 0x0B1838u, 23, 3);
  canvas.fillRect(14, 294, 332, 18, 0x0B1838u);
  char ago[24];
  snprintf(b, sizeof(b), "%s · %s", it.source[0] ? it.source : T("Google News","Google Notícias","Google Noticias"), gfxAgo(it.date, ago, sizeof(ago)));
  gfxTextFit(b, CX, 303, 240, FxInter13, 0x9FC3FFu);
  char tk[700]; tickerText(tk, sizeof(tk), idx, total);
  ticker(312, 26, 0xFFD54Fu, 0x111111u, hm, 0x111111u, 0xFFD54Fu, tk[0] ? tk : it.title, 46);
}

static void drawUrgent(const Item &it)
{
  if(!gfxImage("bg_urgent")) canvas.fillScreen(0x1A0000u);
  const float pulse = 0.5f + 0.5f * sinf(millis() / 1000.0f * 4.0f);
  for(int y = 92; y < 144; y++)
  {
    const float e = fabsf(y - 118) / 26.0f;
    canvas.drawFastHLine(0, y, LCD_W, mix888(0xB71C1Cu, 0xFF2A2Au, (1 - e) * (0.55f + 0.4f * pulse)));
  }
  gfxGlow(CX, 118, 150, 0xFF1E1Eu, (uint8_t)(30 + 40 * pulse));
  gfxTextSpaced("URGENTE", CX, 118, FxOsw34, 0xFFFFFFu, 6);
  gfxWrapRoundCentered(it.title, 212, FxHead18, 0xFFFFFFu, 24, 4, 44);
  char b[48], hm[8]; clockStr(hm, sizeof(hm));
  snprintf(b, sizeof(b), "PLANTÃO · %s", hm);
  const int w = gfxTextWidth(b, FxOsw14);
  canvas.fillSmoothCircle(CX - w / 2 - 10, 293, 4, ((millis() / 500) & 1) ? 0xFF1E1Eu : 0x5A0A0Au);
  gfxText(b, CX + 4, 293, FxOsw14, 0xFFB3B3u);
}

static void draw24h(const Item &it, int idx, int total)
{
  if(!gfxImage("bg_24h")) gfxGradientV(0x0E1A2Eu, 0x0A1424u, 0x08101Eu);
  const uint32_t BLUE = 0x29B6F6u, NAVY = 0x08101Eu;
  char b[96], hm[8]; clockStr(hm, sizeof(hm));
  gfxText("R360", 62, 59, FxOsw18, BLUE, middle_left);
  gfxText("24H", 62 + gfxTextWidth("R360 ", FxOsw18), 59, FxOsw18, 0xFFFFFFu, middle_left);
  gfxText(hm, 298, 59, FxOsw18, 0x9FB8D8u, middle_right);
  strlcpy(b, newsTopicName(it.topic), sizeof(b)); upperUtf8b(b);
  canvas.fillRect(30, 84, 300, 24, BLUE);
  gfxText(b, 40, 96, FxOsw14, NAVY, middle_left);
  canvas.fillRect(30, 108, 300, 94, 0xF4F7FAu);
  wrapBox(it.title, 40, 114, 280, FxHead18, NAVY, 21, 4);
  // duas proximas manchetes em cartoes menores
  static const uint32_t TAGC[] = {0xFFB300u, 0x66BB6Au};
  lock(); Item n2[2]; const int nn = min(2, total - 1); for(int k = 0; k < nn; k++) n2[k] = items[(idx + 1 + k) % total]; unlock();
  for(int k = 0; k < nn; k++)
  {
    const int x = 30 + k * 152;
    gfxBlendRect(x, 210, 148, 62, 0xFFFFFFu, 22);
    canvas.fillRect(x, 210, 148, 3, TAGC[k]);
    strlcpy(b, newsTopicName(n2[k].topic), sizeof(b)); upperUtf8b(b);
    gfxText(b, x + 8, 222, FxInter13, TAGC[k], middle_left);
    wrapBox(n2[k].title, x + 8, 230, 134, FxInter13, 0xE8EEF6u, 15, 2);
  }
  char tk[700]; tickerText(tk, sizeof(tk), idx + 2, total);
  ticker(282, 24, BLUE, NAVY, "ÚLTIMAS", NAVY, BLUE, tk[0] ? tk : it.title, 46);
  snprintf(b, sizeof(b), "%d de %d · %s", idx + 1, total, it.source[0] ? it.source : T("Google News","Google Notícias","Google Noticias"));
  gfxTextFit(b, CX, 321, 180, FxInter13, 0x7F95B5u);
}

static void drawTeletext(const Item &it, int idx, int total)
{
  canvas.fillScreen(TFT_BLACK);
  const uint32_t Y = 0xFFFF00u, C = 0x00FFFFu, W = 0xFFFFFFu, M = 0xFF00FFu;
  char b[96], hm[12];
  const time_t t = time(nullptr); struct tm tm; localtime_r(&t, &tm);
  snprintf(hm, sizeof(hm), "%02d%c%02d", tm.tm_hour, (tm.tm_sec & 1) ? ':' : ' ', tm.tm_min);
  // cabecalho
  gfxFont(FxVT22);
  snprintf(b, sizeof(b), " P%d ", 100 + idx);
  int x = 62; const int y0 = 58;
  int w = canvas.textWidth(b); canvas.fillRect(x, y0 - 10, w, 20, 0x0000FFu); gfxText(b, x, y0, FxVT22, Y, middle_left); x += w + 6;
  gfxText("R360 TEXTO", x, y0, FxVT22, C, middle_left);
  gfxText(hm, 300, y0, FxVT22, W, middle_right);
  // barras coloridas
  static const struct { const char *t; uint32_t bg, fg; } BARS[] = {{" NOTICIAS ", 0xFF0000u, W}, {" CLIMA ", 0x00FF00u, 0x000000u}, {" VOOS ", 0xFFFF00u, 0x000000u}};
  x = 52;
  for(auto &br : BARS) { w = canvas.textWidth(br.t); canvas.fillRect(x, 74, w, 20, br.bg); gfxText(br.t, x, 84, FxVT22, br.fg, middle_left); x += w; }
  // manchete em maiusculas, amarela
  char tt[sizeof(it.title)]; strlcpy(tt, it.title, sizeof(tt)); upperUtf8b(tt);
  wrapBox(it.title[0] ? tt : "SEM MANCHETES", 48, 106, 264, FxVT22, Y, 22, 5);
  char ago[24];
  snprintf(b, sizeof(b), "%s %s", it.source[0] ? it.source : "Google", gfxAgo(it.date, ago, sizeof(ago)));
  gfxTextFit(b, CX, 230, 250, FxVT22, W);
  // indice de paginas
  lock();
  for(int k = 1; k <= 2 && k < total; k++)
  {
    char tt[40]; strlcpy(tt, items[(idx + k) % total].title, sizeof(tt)); upperUtf8b(tt);
    snprintf(b, sizeof(b), "%d %s", 100 + (idx + k) % total, tt);
    gfxTextFit(b, CX, 240 + k * 24, 240 - k * 30, FxVT22, k == 1 ? C : M);
  }
  unlock();
  // linhas de varredura
  uint16_t *buf = (uint16_t *)canvas.getBuffer();
  for(int y = 2; y < LCD_H; y += 3) for(int i = 0; i < LCD_W; i++) { uint16_t &p = buf[y * LCD_W + i]; p = gfxBlend(p, 0x000000u, 90); }
}

bool newsAnimated()
{
  if(qr) return false;
  if(feat.nStyle == 2 || feat.nStyle == 3 || feat.nStyle == 4) return true;
  if(feat.nUrgent && feat.nStyle != 5)
  {
    lock(); const bool u = itemCount && isUrgent(items[cur % itemCount].title); unlock();
    return u;
  }
  return false;
}

void newsDraw()
{
  if(feat.nStyle == 0 && !qr) { if(!gfxImage("bg_aurora")) gfxGradientV(0x03040Eu, 0x0A0C2Au, 0x141238u); }
  else gfxBackground();
  lock();
  if(!itemCount) { char s[64]; strlcpy(s, status, sizeof(s)); unlock(); gfxStatusLine(s, C_MUTED); return; }
  const Item it = items[cur % itemCount];
  const int idx = cur % itemCount, total = itemCount;
  unlock();

  if(qr)
  {
    if(!it.link[0]) { qr = false; return; }
    const int size = 210, x = CX - size / 2, y = 75;
    canvas.fillSmoothRoundRect(x - 10, y - 10, size + 20, size + 20, 14, TFT_WHITE);
    canvas.qrcode(it.link, x, y, size, 3);
    gfxText(T("Open on your phone","Abra no celular","Abre en el celular"), CX, 46, FontBody, C_ACCENT);
    gfxTextFit(it.source, CX, 318, 220, FontSmall, C_MUTED);
    return;
  }

  // plantao automatico: manchete urgente vira tarja vermelha (exceto no teletexto)
  if(feat.nStyle == 3 || (feat.nUrgent && feat.nStyle != 5 && isUrgent(it.title))) { drawUrgent(it); return; }
  if(feat.nStyle == 2) { drawGC(it, idx, total); return; }
  if(feat.nStyle == 4) { draw24h(it, idx, total); return; }
  if(feat.nStyle == 5) { drawTeletext(it, idx, total); return; }
  if(feat.nStyle == 0) { drawAurora(it, idx, total); return; }
  char b[96], ago[24];
  snprintf(b, sizeof(b), "%s · %s", it.source[0] ? it.source : newsTopicName(it.topic), gfxAgo(it.date, ago, sizeof(ago)));
  gfxTextFit(b, CX, 60, 230, FontSmall, C_ACCENT);

  // Manchete grande; se nao couber, fonte menor
  int need = 0;
  gfxWrapRoundCentered(it.title, CY - 4, FontTitle, C_TEXT, 36, 5, 26, false, &need);
  if(need <= 5) gfxWrapRoundCentered(it.title, CY - 4, FontTitle, C_TEXT, 36, 5, 26);
  else gfxWrapRoundCentered(it.title, CY - 4, FontBody, C_TEXT, 28, 7, 26);
  snprintf(b, sizeof(b), "%d/%d · %s", idx + 1, total, newsTopicName(it.topic));
  gfxText(b, CX, 306, FontSmall, C_MUTED);
}
