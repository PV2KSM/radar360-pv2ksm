// Tela Cotacoes: ate 10 moedas/cripto contra a moeda local, em rodizio (uma por vez, grande, com variacao do
// dia e grafico dos ultimos 15 dias) ou em plaquinha (lista toda). Dados: AwesomeAPI (gratis, sem chave;
// sem chave a resposta vem de um cache de 1 minuto e o limite e 100 mil pedidos por mes).
#include "Quotes.h"
#include "Lang.h"
#include "Gfx.h"
#include "Feat.h"
#include "Http.h"
#include "Net.h"
#include "Crash.h"
#include <ArduinoJson.h>
#include <math.h>
#include <time.h>

static const int HN = 15;
struct Quote
{
  char code[8];
  char name[28];
  float bid, pct, high, low;
  time_t ts;
  float hist[HN]; int hn;      // fechamento dos ultimos dias (mais antigo primeiro)
  uint32_t histAt;
  bool ok;
};
static Quote q[QUOTES_MAX];
static int qn = 0;
static uint32_t fetchAt = 0, lastOk = 0;
static uint32_t gen = 0xFFFFFFFF;
static bool open_ = false;
static int cur = 0;
static uint32_t shownAt = 0;
static char status[72] = "";
static SemaphoreHandle_t mtx = nullptr;
static void lock() { if(mtx) xSemaphoreTake(mtx, portMAX_DELAY); }
static void unlock() { if(mtx) xSemaphoreGive(mtx); }

void quotesBegin() { mtx = xSemaphoreCreateMutex(); }
void quotesOpen() { open_ = true; shownAt = millis(); if(millis() - lastOk > 60000UL) fetchAt = 0; }
void quotesClose() { open_ = false; }
const char *quotesStatus() { return status; }

// Nomes em portugues dos codigos mais comuns (o resto usa o nome que a API manda)
static const char *ptName(const char *c)
{
  static const char *const T[][2] = {
    {"USD", "Dólar"}, {"EUR", "Euro"}, {"GBP", "Libra"}, {"JPY", "Iene"}, {"ARS", "Peso argentino"}, {"CAD", "Dólar canadense"},
    {"AUD", "Dólar australiano"}, {"CHF", "Franco suíço"}, {"CNY", "Yuan"}, {"BTC", "Bitcoin"}, {"ETH", "Ethereum"},
    {"LTC", "Litecoin"}, {"XRP", "XRP"}, {"DOGE", "Dogecoin"}, {"SOL", "Solana"}, {"BRL", "Real"}, {"MXN", "Peso mexicano"},
    {"CLP", "Peso chileno"}, {"UYU", "Peso uruguaio"}, {"PYG", "Guarani"}, {"ILS", "Shekel"}, {"XAU", "Ouro"}, {"XAG", "Prata"}};
  for(auto &t : T) if(!strcmp(t[0], c)) return t[1];
  return nullptr;
}

static const char *symbol(const char *local)
{
  if(!strcmp(local, "BRL")) return "R$";
  if(!strcmp(local, "USD")) return "US$";
  if(!strcmp(local, "EUR")) return "€";
  if(!strcmp(local, "GBP")) return "£";
  if(!strcmp(local, "JPY")) return "¥";
  return local;
}

// Numero no formato brasileiro (1.234,56); casas conforme o tamanho
static void fmtNum(char *b, size_t n, float v)
{
  const int dec = v >= 1000 ? 0 : v >= 1 ? 2 : 4;
  char raw[32]; snprintf(raw, sizeof(raw), "%.*f", dec, (double)v);
  char *dot = strchr(raw, '.');
  const int intLen = dot ? (int)(dot - raw) : (int)strlen(raw);
  size_t j = 0;
  for(int i = 0; i < intLen && j + 2 < n; i++)
  {
    b[j++] = raw[i];
    const int left = intLen - 1 - i;
    if(left > 0 && left % 3 == 0 && raw[i] != '-') b[j++] = '.';
  }
  if(dot && j + 1 < n) { b[j++] = ','; for(const char *p = dot + 1; *p && j + 1 < n; p++) b[j++] = *p; }
  b[j] = 0;
}

static void fmtPrice(char *b, size_t n, float v)
{
  char num[32]; fmtNum(num, sizeof(num), v);
  snprintf(b, n, "%s %s", symbol(feat.qLocal), num);
}

static void setStatus(const char *s) { lock(); strlcpy(status, s, sizeof(status)); unlock(); }

// ---- Rede ----
static void fetchLast()
{
  char url[220]; size_t u = snprintf(url, sizeof(url), "https://economia.awesomeapi.com.br/json/last/");
  int asked = 0;
  for(int i = 0; i < feat.qCount; i++)
  {
    if(!feat.qItems[i][0] || !strcmp(feat.qItems[i], feat.qLocal)) continue;
    u += snprintf(url + u, sizeof(url) - u, "%s%s-%s", asked ? "," : "", feat.qItems[i], feat.qLocal);
    asked++;
  }
  if(!asked) { setStatus(T("Pick quotes in Features → Quotes","Escolha as cotações em Recursos → Cotações","Elige cotizaciones en Funciones → Cotizaciones")); return; }
  HttpBuf buf; char err[40];
  crumbWorker("cotacoes");
  const int code = httpGet(url, buf, 24 * 1024, err, sizeof(err), "application/json");
  JsonDocument doc;
  if(code == 404 || code == 400)
  {
    // um par que nao existe derruba o pedido inteiro: avisa qual
    if(code == 404 && !deserializeJson(doc, buf.str(), buf.len)) { char s[72]; snprintf(s, sizeof(s), T("Pair not found: %s","Par não encontrado: %s","Par no encontrado: %s"), (const char *)(doc["message"] | T("check the codes","confira os códigos","revisa los códigos"))); setStatus(s); }
    else setStatus(T("Some code does not exist: check in Features → Quotes","Algum código não existe: confira em Recursos → Cotações","Algún código no existe: revisa en Funciones → Cotizaciones"));
    return;
  }
  if(code != 200) { char s[72]; snprintf(s, sizeof(s), T("Quotes: %s","Cotações: %s","Cotizaciones: %s"), err); setStatus(s); return; }
  if(deserializeJson(doc, buf.str(), buf.len)) { setStatus(T("Quotes: invalid response","Cotações: resposta inválida","Cotizaciones: respuesta inválida")); return; }
  lock();
  // mantem o historico de quem continua na lista
  Quote old[QUOTES_MAX]; const int on = qn; memcpy(old, q, sizeof(q));
  qn = 0;
  for(int i = 0; i < feat.qCount && qn < QUOTES_MAX; i++)
  {
    const char *c = feat.qItems[i];
    if(!c[0] || !strcmp(c, feat.qLocal)) continue;
    char key[16]; snprintf(key, sizeof(key), "%s%s", c, feat.qLocal);
    JsonObjectConst o = doc[key];
    Quote &d = q[qn++];
    memset(&d, 0, sizeof(d));
    strlcpy(d.code, c, sizeof(d.code));
    for(int k = 0; k < on; k++) if(!strcmp(old[k].code, c)) { memcpy(d.hist, old[k].hist, sizeof(d.hist)); d.hn = old[k].hn; d.histAt = old[k].histAt; }
    if(o.isNull()) continue;
    d.bid = atof(o["bid"] | "0"); d.pct = atof(o["pctChange"] | "0");
    d.high = atof(o["high"] | "0"); d.low = atof(o["low"] | "0");
    d.ts = (time_t)atol(o["timestamp"] | "0");
    const char *pn = ptName(c);
    if(pn) strlcpy(d.name, pn, sizeof(d.name));
    else { strlcpy(d.name, o["name"] | c, sizeof(d.name)); char *sl = strchr(d.name, '/'); if(sl) *sl = 0; }
    d.ok = d.bid > 0;
  }
  if(cur >= qn) cur = 0;
  status[0] = 0;
  unlock();
  lastOk = millis();
}

static void fetchHist(int i)
{
  char code[8]; lock(); strlcpy(code, q[i].code, sizeof(code)); unlock();
  char url[120]; snprintf(url, sizeof(url), "https://economia.awesomeapi.com.br/json/daily/%s-%s/%d", code, feat.qLocal, HN);
  HttpBuf buf; char err[40];
  crumbWorker("cotacoes:historico");
  if(httpGet(url, buf, 16 * 1024, err, sizeof(err), "application/json") != 200) { lock(); q[i].histAt = millis(); unlock(); return; }
  JsonDocument filter; filter[0]["bid"] = true;
  JsonDocument doc;
  if(deserializeJson(doc, buf.str(), buf.len, DeserializationOption::Filter(filter))) return;
  JsonArrayConst a = doc.as<JsonArrayConst>();
  float h[HN]; int n = 0;
  for(JsonVariantConst v : a) { if(n >= HN) break; h[n++] = atof(v["bid"] | "0"); }   // mais novo primeiro
  lock();
  if(i < qn && !strcmp(q[i].code, code))
  {
    q[i].hn = n;
    for(int k = 0; k < n; k++) q[i].hist[k] = h[n - 1 - k];
    q[i].histAt = millis();
  }
  unlock();
}

void quotesWork()
{
  if(!feat.scrQuotes || !netConnected()) return;
  if(gen != featGen) { gen = featGen; fetchAt = 0; lock(); qn = 0; unlock(); }
  if(!open_ && qn) return;   // fora da tela: so busca de novo quando abrir
  if(!fetchAt || (int32_t)(millis() - fetchAt) >= 0)
  {
    fetchAt = millis() + max(1, (int)feat.qRefresh) * 60000UL;
    fetchLast();
    return;
  }
  // grafico dos ultimos dias: um por vez, o que esta na tela primeiro (vale por 1 hora)
  if(!open_) return;
  for(int k = 0; k < qn; k++)
  {
    const int i = (cur + k) % qn;
    lock(); const bool stale = q[i].ok && (!q[i].histAt || millis() - q[i].histAt > 3600000UL); unlock();
    if(stale) { fetchHist(i); return; }
    if(feat.qStyle == 1) break;   // plaquinha nao mostra grafico
  }
}

bool quotesTick()
{
  if(feat.qStyle != 0 || qn < 2) return false;
  if(millis() - shownAt < max(2, (int)feat.qSecs) * 1000UL) return false;
  shownAt = millis();
  lock(); cur = (cur + 1) % qn; unlock();
  return true;
}

// ---- Desenho ----
static const uint32_t UP = 0x58E89Au, DOWN = 0xFF6B6Bu;

static void needData()
{
  gfxBackground();
  gfxText(T("Quotes","Cotações","Cotizaciones"), CX, 120, FontTitle, C_ACCENT);
  char s[72]; lock(); strlcpy(s, status[0] ? status : (netConnected() ? T("Fetching quotes…","Buscando cotações…","Buscando cotizaciones…") : T("No internet","Sem internet","Sin internet")), sizeof(s)); unlock();
  gfxWrapRoundCentered(s, 200, FontSmall, C_MUTED, 24, 3, 40, true, nullptr);
}

static void pctText(char *b, size_t n, float p)
{
  char v[16]; snprintf(v, sizeof(v), "%.2f", fabsf(p)); for(char *c = v; *c; c++) if(*c == '.') *c = ',';
  snprintf(b, n, "%s %s%%", p > 0.005f ? "↑" : p < -0.005f ? "↓" : "·", v);
}

static void drawOne(const Quote &d)
{
  gfxBackground();
  const bool up = d.pct >= 0;
  const uint32_t col = d.pct > 0.005f ? UP : d.pct < -0.005f ? DOWN : C_MUTED;
  gfxGlow(CX, 150, 150, col, 22);
  char b[48];
  gfxText(d.name, CX, 64, FontBody, C_TEXT);
  snprintf(b, sizeof(b), "%s → %s", d.code, feat.qLocal);
  gfxText(b, CX, 92, FontSmall, C_MUTED);
  fmtPrice(b, sizeof(b), d.bid);
  gfxTextFit(b, CX, 140, 300, FxOsw34 != FontTitle ? FxOsw34 : FontTitle, 0xFFFFFFu);
  pctText(b, sizeof(b), d.pct);
  char line[64]; snprintf(line, sizeof(line), T("%s today","%s hoje","%s hoy"), b);
  gfxText(line, CX, 182, FontBody, col);
  // grafico dos ultimos dias
  const int gx0 = 70, gx1 = 290, gy0 = 212, gy1 = 278;
  if(d.hn >= 2)
  {
    float mn = 1e30f, mx = -1e30f;
    for(int k = 0; k < d.hn; k++) { mn = min(mn, d.hist[k]); mx = max(mx, d.hist[k]); }
    if(mx - mn < 1e-9f) { mx += 1; mn -= 1; }
    const uint32_t lc = d.hist[d.hn - 1] >= d.hist[0] ? UP : DOWN;
    auto px = [&](int k) { return gx0 + (gx1 - gx0) * k / (d.hn - 1); };
    auto py = [&](float v) { return gy1 - (int)((gy1 - gy0) * (v - mn) / (mx - mn)); };
    for(int k = 0; k + 1 < d.hn; k++)
    {
      const int xa = px(k), xb = px(k + 1), ya = py(d.hist[k]), yb = py(d.hist[k + 1]);
      for(int x = xa; x <= xb; x++)
      {
        const int y = ya + (yb - ya) * (x - xa) / max(1, xb - xa);
        for(int yy = y; yy <= gy1; yy++) gfxBlendPixel(x, yy, lc, (uint8_t)(70 * (gy1 - yy + 1) / (gy1 - y + 1)));
      }
      canvas.drawWideLine(xa, ya, xb, yb, 1.4f, lc);
    }
    canvas.fillSmoothCircle(px(d.hn - 1), py(d.hist[d.hn - 1]), 3.5f, lc);
  }
  else gfxText(T("chart loading…","gráfico carregando…","gráfico cargando…"), CX, 246, FxInter13, C_MUTED);
  // maxima / minima do dia
  if(d.high > 0 && d.low > 0)
  {
    char h[24], l[24]; fmtNum(h, sizeof(h), d.high); fmtNum(l, sizeof(l), d.low);
    snprintf(b, sizeof(b), T("today: high %s · low %s","hoje: máx %s · mín %s","hoy: máx %s · mín %s"), h, l);
    gfxTextFit(b, CX, 302, 230, FxInter13, C_MUTED);
  }
  (void)up;
}

static void drawBoard()
{
  gfxBackground();
  char hb[32]; snprintf(hb, sizeof(hb), T("Quotes in %s","Cotações em %s","Cotizaciones en %s"), symbol(feat.qLocal));
  gfxText(hb, CX, 44, FontBody, C_ACCENT);
  const int n = qn;
  const bool big = n <= 5;
  const int rowH = big ? 44 : n <= 7 ? 34 : 26;
  const uint8_t *fn = big ? FontBody : FontSmall;
  const int top = CY - rowH * n / 2 + rowH / 2 + 12;
  // colunas alinhadas: largura da linha mais estreita (a primeira ou a ultima)
  const int hw = min(gfxRoundHalfWidth(top - rowH / 2, 22), gfxRoundHalfWidth(top + (n - 1) * rowH + rowH / 2, 22));
  for(int i = 0; i < n; i++)
  {
    const Quote &d = q[i];
    const int y = top + i * rowH;
    const int xl = CX - hw, xr = CX + hw;
    if(i) canvas.drawFastHLine(xl + 8, y - rowH / 2, 2 * hw - 16, 0x14302Bu);
    gfxText(d.code, xl + 10, y, fn, C_TEXT, middle_left);
    char b[40], p[24];
    if(d.ok) { fmtNum(b, sizeof(b), d.bid); pctText(p, sizeof(p), d.pct); }
    else { strlcpy(b, "—", sizeof(b)); p[0] = 0; }
    const uint32_t col = d.pct > 0.005f ? UP : d.pct < -0.005f ? DOWN : C_MUTED;
    const int pw = big ? 96 : 84;
    gfxText(b, xr - pw - 6, y, fn, 0xFFFFFFu, middle_right);
    gfxText(p, xr - 4, y, big ? FontSmall : FxInter13, col, middle_right);
  }
  time_t now = time(nullptr); struct tm t; localtime_r(&now, &t);
  char b[40]; snprintf(b, sizeof(b), "%s · %02d:%02d", "AwesomeAPI", t.tm_hour, t.tm_min);
  gfxText(b, CX, 336, FxInter13, C_MUTED);
}

void quotesDraw()
{
  lock();
  if(!qn) { unlock(); needData(); return; }
  if(feat.qStyle == 1) drawBoard();
  else
  {
    if(cur >= qn) cur = 0;
    const Quote d = q[cur];
    unlock();
    if(d.ok) drawOne(d);
    else { gfxBackground(); gfxText(d.code, CX, 150, FontTitle, C_TEXT); gfxText(T("no quote","sem cotação","sin cotización"), CX, 196, FontSmall, C_MUTED); }
    return;
  }
  unlock();
}
