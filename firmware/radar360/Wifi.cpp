// Tela Wi-Fi: "explorador" das redes 2.4 GHz ao redor. Passivo (so escuta as transmissoes publicas).
// Pagina 1: lista das redes (nome, sinal, canal, cadeado). Pagina 2: congestionamento por canal, com
// sugestao do canal mais livre. Usa a varredura do modulo Net (netRequestScan), so enquanto a tela esta
// aberta. O ESP32-S3 so tem radio 2.4 GHz, entao redes 5 GHz nao aparecem.
#include "Wifi.h"
#include "Lang.h"
#include "Gfx.h"
#include "Net.h"
#include "Config.h"
#include "Feat.h"
#include <WiFi.h>

static bool open_ = false;
static int page = 0;                 // 0 lista, 1 canais
static uint32_t scanAt = 0;
static int scroll = 0;

void wifiOpen() { open_ = true; page = 0; scroll = 0; scanAt = 0; }
void wifiClose() { open_ = false; }
bool wifiShortPress() { page ^= 1; scroll = 0; return true; }   // toque curto alterna lista/canais

void wifiWork()
{
  if(!open_ || !feat.scrWifi) return;
  // pede uma varredura ao abrir e a cada ~8 s (a varredura suspende o Wi-Fi por um instante)
  if((!scanAt || (int32_t)(millis() - scanAt) >= 0) && !netScanRunning())
  {
    scanAt = millis() + 8000;
    netRequestScan();
  }
}

bool wifiAnimated() { return open_ && netScanRunning(); }

// barrinhas de sinal ▂▄▆█ conforme o dBm
static uint32_t rssiColor(int r) { return r >= -60 ? C_ACCENT : r >= -75 ? C_WARN : C_BAD; }
static int rssiBars(int r) { return r >= -55 ? 4 : r >= -67 ? 3 : r >= -78 ? 2 : 1; }

static void drawBars(int x, int y, int bars, uint32_t col)
{
  for(int i = 0; i < 4; i++)
  {
    const int h = 4 + i * 4;
    const uint32_t c = i < bars ? col : C_RING;
    canvas.fillRoundRect(x + i * 7, y - h / 2, 5, h, 1, c);
  }
}

static void drawList()
{
  gfxBackground();
  const ScanEntry *e; const int n = netScanResults(&e);
  const int hidden = netScanHidden();
  char head[48];
  if(n) snprintf(head, sizeof(head), T("Wi-Fi · %d networks%s","Wi-Fi · %d redes%s","Wi-Fi · %d redes%s"), n, hidden ? "" : "");
  else strlcpy(head, "Wi-Fi", sizeof(head));
  gfxText(head, CX, 40, FontBody, C_ACCENT);
  if(hidden) { char h[32]; snprintf(h, sizeof(h), T("+%d hidden","+%d ocultas","+%d ocultas"), hidden); gfxText(h, CX, 60, FxInter13, C_MUTED); }

  if(!n)
  {
    gfxText(netScanRunning() ? T("Scanning networks…","Procurando redes…","Buscando redes…") : T("No networks found","Nenhuma rede encontrada","Sin redes"), CX, 170, FontSmall, C_MUTED);
    gfxText("Toque: ver canais", CX, 300, FxInter13, C_MUTED);
    return;
  }

  const int rows = min(n, 6);
  const int top = 92, rowH = 34;
  char myssid[33] = ""; if(netConnected()) strlcpy(myssid, WiFi.SSID().c_str(), sizeof(myssid));
  for(int i = 0; i < rows && i + scroll < n; i++)
  {
    const ScanEntry &w = e[i + scroll];
    const int y = top + i * rowH;
    const int hw = gfxRoundHalfWidth(y, 20);
    const int xl = CX - hw, xr = CX + hw;
    const bool mine = myssid[0] && !strcmp(w.ssid, myssid);
    if(mine) canvas.fillRoundRect(xl, y - rowH / 2 + 2, 2 * hw, rowH - 4, 8, 0x0E2420u);
    char nm[26]; strlcpy(nm, w.ssid, sizeof(nm));
    gfxTextFit(nm, xl + 14, y - 6, 2 * hw - 120, FontSmall, mine ? C_ACCENT : C_TEXT, middle_left);
    char sub[40]; snprintf(sub, sizeof(sub), T("channel %d%s","canal %d%s","canal %d%s"), w.channel, w.secure ? "" : T(" · open"," · aberta"," · abierta"));
    gfxText(sub, xl + 14, y + 10, FxInter13, w.secure ? C_MUTED : C_WARN, middle_left);
    drawBars(xr - 36, y - 4, rssiBars(w.rssi), rssiColor(w.rssi));
    char db[12]; snprintf(db, sizeof(db), "%d", w.rssi);
    gfxText(db, xr - 6, y + 10, FxInter13, C_MUTED, middle_right);
    if(!w.secure) {} // aberta ja sinalizada
  }
  // rodape
  char foot[40];
  if(n > 6) snprintf(foot, sizeof(foot), T("up/down: scroll · tap: channels","cima/baixo: rolar · toque: canais","arriba/abajo: desplazar · toca: canales"));
  else snprintf(foot, sizeof(foot), T("tap: channels","toque: ver canais","toca: canales"));
  gfxText(foot, CX, 324, FxInter13, C_MUTED);
}

static void drawChannels()
{
  gfxBackground();
  gfxText(T("2.4 GHz channels","Canais 2.4 GHz","Canales 2.4 GHz"), CX, 40, FontBody, C_ACCENT);
  uint8_t ap[14]; int8_t rssi[14];
  netChannelLoad(ap, rssi);
  int maxAp = 1;
  for(int c = 1; c <= 13; c++) if(ap[c] > maxAp) maxAp = ap[c];
  // peso de congestionamento: redes do canal + vizinhos (2.4 GHz se sobrepoe ~±2 canais)
  float load[14] = {0};
  float worst = 0.01f;
  for(int c = 1; c <= 13; c++)
  {
    for(int d = -2; d <= 2; d++) { const int k = c + d; if(k >= 1 && k <= 13) load[c] += ap[k] * (d == 0 ? 1.0f : d == 1 || d == -1 ? 0.6f : 0.3f); }
    if(load[c] > worst) worst = load[c];
  }
  const int myCh = netMyChannel();
  // melhor canal (menor carga), preferindo 1/6/11 em empate
  int best = 1; float bestL = 1e9f;
  for(int c : {1, 6, 11, 2, 3, 4, 5, 7, 8, 9, 10, 12, 13})
    if(load[c] < bestL - 0.01f) { bestL = load[c]; best = c; }

  const int gx0 = 44, gx1 = 316, gy1 = 250, gyTop = 92;
  const int bw = (gx1 - gx0) / 13;
  for(int c = 1; c <= 13; c++)
  {
    const int x = gx0 + (c - 1) * bw;
    const int h = (int)(load[c] / worst * (gy1 - gyTop));
    const bool mine = c == myCh;
    const bool rec = c == best;
    uint32_t col = rec ? C_ACCENT : mine ? C_WARN : 0x2C5A52u;
    if(h > 2) canvas.fillRoundRect(x + 1, gy1 - h, bw - 2, h, 2, col);
    else canvas.fillRoundRect(x + 1, gy1 - 2, bw - 2, 2, 1, C_RING);
    // numero do canal
    char cc[4]; snprintf(cc, sizeof(cc), "%d", c);
    gfxText(cc, x + bw / 2, gy1 + 14, FxInter13, mine || rec ? col : C_MUTED);
  }
  // legenda
  char l1[48];
  if(myCh) snprintf(l1, sizeof(l1), T("Your channel: %d","Seu canal: %d","Tu canal: %d"), myCh);
  else strlcpy(l1, T("Not connected","Não conectado","No conectado"), sizeof(l1));
  gfxText(l1, CX, 286, FontSmall, C_WARN);
  char l2[48]; snprintf(l2, sizeof(l2), T("Freest: channel %d","Mais livre: canal %d","Más libre: canal %d"), best);
  gfxText(l2, CX, 312, FontSmall, C_ACCENT);
  gfxText(T("tap: networks","toque: ver redes","toca: redes"), CX, 334, FxInter13, C_MUTED);
}

void wifiDraw() { if(page == 1) drawChannels(); else drawList(); }

void wifiScroll(int d)
{
  if(page != 0) return;
  const ScanEntry *e; const int n = netScanResults(&e);
  scroll = constrain(scroll + d, 0, max(0, n - 6));
}
