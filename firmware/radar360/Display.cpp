#include "Display.h"
#include <esp_heap_caps.h>
#include <esp_system.h>

LGFX lcd;
LGFX_Sprite canvas(&lcd);

// Dois quadros na PSRAM: enquanto um vai para a tela por DMA, o proximo ja e desenhado no outro.
// O fio da tela e de 1 bit: a 40 MHz um quadro inteiro (253 KB) leva ~52 ms; a 80 MHz, ~26 ms.
// Por isso so vai o retangulo que mudou (linhas E colunas), e cortado no circulo da tela: os cantos do
// quadrado nunca aparecem. O envio e feito por uma tarefa propria, em faixas (cada faixa com a largura
// do circulo naquela altura), enquanto a interface ja desenha o quadro seguinte.
static uint16_t *bufs[2] = {nullptr, nullptr};
static int drawIdx = 0;          // quadro em que o canvas desenha agora
static bool inFlight = false;
static uint16_t *frontBuf = nullptr;
static bool fullNext = true;
static uint32_t busHz = 40000000;
static DisplayStats st = {};

// Tarefa de envio
// Cada envio e uma lista de retangulos: faixas de 40 linhas x trechos de colunas que mudaram (blocos de 24 px),
// ja cortados no circulo. Partes distantes (segundos no centro + anel na borda) vao separadas.
struct Rect { int16_t x0, x1, y0, y1; };
static const int MAX_RECTS = 96;
struct Job { const uint16_t *buf; int n; Rect r[MAX_RECTS]; };
static Job job;
static TaskHandle_t pushTask = nullptr;
static SemaphoreHandle_t doneSem = nullptr;

// TE (tearing effect): o painel avisa quando comeca a varrer a tela; envios curtos esperam esse momento
static SemaphoreHandle_t teSem = nullptr;
static volatile uint32_t teCount = 0;
static bool teOk = false;
static uint32_t teCheckAt = 0, teCountAt = 0;
static void IRAM_ATTR teIsr()
{
  teCount++;
  BaseType_t w = pdFALSE;
  if(teSem) xSemaphoreGiveFromISR(teSem, &w);
  if(w) portYIELD_FROM_ISR();
}

static const int BAND = 40;      // altura das faixas do circulo

// Colunas visiveis (com 1 px de folga) numa faixa de linhas: a linha mais perto do centro manda
static void chordFor(int ya, int yb, int &l, int &r)
{
  int best = 1000;
  for(int y = ya; y <= yb; y++) { const int d = abs(2 * y + 1 - LCD_H); if(d < best) best = d; }   // distancia ao centro x2
  const float dy = best * 0.5f, R = LCD_W * 0.5f;
  if(dy >= R) { l = 1; r = 0; return; }
  const int half = (int)ceilf(sqrtf(R * R - dy * dy)) + 1;
  l = max(0, LCD_W / 2 - half); r = min(LCD_W - 1, LCD_W / 2 + half - 1);
}

static void runJob()
{
  // tempo estimado do envio (para decidir se espera o TE)
  uint32_t bytes = 0;
  for(int i = 0; i < job.n; i++) bytes += (job.r[i].x1 - job.r[i].x0 + 1) * (job.r[i].y1 - job.r[i].y0 + 1) * 2;
  if(!bytes) return;
  const uint32_t mhz = busHz / 1000000;
  const uint32_t estUs = bytes * 8 / mhz;
  if(teOk && estUs < 8000)
  {
    xSemaphoreTake(teSem, 0);                      // descarta um aviso antigo
    xSemaphoreTake(teSem, pdMS_TO_TICKS(20));      // espera o proximo inicio de varredura
    st.teWaits++;
  }
  lcd.startWrite();
  // Esta tarefa tem prioridade acima da tarefa de dados e da ociosa do nucleo 0: nunca pode ficar mais que
  // alguns ms seguidos sem dormir, senao sufoca o nucleo (foi o "watchdog de tarefa" da v0.17.0 ao mudar o
  // alcance do mapa: muitos pedacos pequenos, e a espera entre eles girava em vez de dormir).
  uint32_t awakeSince = micros();
  uint32_t lastUs = 0, lastAt = 0;      // duracao estimada e inicio do pedaco anterior
  for(int i = 0; i < job.n; i++)
  {
    const Rect &q = job.r[i];
    const int h = q.y1 - q.y0 + 1, w = q.x1 - q.x0 + 1;
    if(lcd.dmaBusy())
    {
      // dorme o tempo que ainda falta do pedaco anterior (o resto, menos de 1 tick, gira)
      const int32_t left = (int32_t)(lastUs - (micros() - lastAt));
      if(left > 1500) { vTaskDelay(pdMS_TO_TICKS(left / 1000)); awakeSince = micros(); }
      const uint32_t t0 = micros();
      while(lcd.dmaBusy() && micros() - t0 < 60000) { if(micros() - t0 > 2000) { vTaskDelay(1); awakeSince = micros(); } }
    }
    if(micros() - awakeSince > 3000) { vTaskDelay(1); awakeSince = micros(); }   // pausa garantida
    lcd.setClipRect(q.x0, q.y0, w, h);
    lastAt = micros();
    lastUs = (uint32_t)w * h * 16 / mhz + 30;
    lcd.pushImageDMA(0, q.y0, LCD_W, h, (const lgfx::swap565_t *)(job.buf + q.y0 * LCD_W));
    st.rows += h; st.px += w * h;
  }
  while(lcd.dmaBusy()) vTaskDelay(1);
  lcd.waitDMA();
  lcd.clearClipRect();
  lcd.endWrite();
}

static void pushTaskFn(void *)
{
  for(;;)
  {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    runJob();
    xSemaphoreGive(doneSem);
  }
}

static void finishDma()
{
  if(!inFlight) return;
  const uint32_t t = micros();
  if(pushTask) xSemaphoreTake(doneSem, portMAX_DELAY);
  else { lcd.waitDMA(); lcd.endWrite(); }
  st.waitUs += micros() - t;
  inFlight = false;
}

void displayInvalidate() { fullNext = true; }

void displaySetTurbo(bool on)
{
  const uint32_t hz = on ? 80000000 : 40000000;
  if(hz == busHz) return;
  finishDma();
  busHz = hz;
  lcd.setBusHz(hz);
  fullNext = true;
}

uint8_t displayBusMHz() { return busHz / 1000000; }
const DisplayStats &displayStats()
{
  // TE: liga a sincronia so se o painel estiver mesmo mandando o sinal (~60 por segundo)
  const uint32_t now = millis();
  if(now - teCheckAt >= 2000)
  {
    const uint32_t c = teCount;
    st.teHz = (c - teCountAt) * 1000 / max((uint32_t)1, (uint32_t)(now - teCheckAt));
    teOk = st.teHz >= 20 && st.teHz <= 150;
    teCheckAt = now; teCountAt = c;
  }
  return st;
}

bool displayBegin()
{
  lcd.init();
  lcd.setBrightness(0);
  lcd.fillScreen(TFT_BLACK);
  const size_t n = LCD_W * LCD_H * 2;
  for(auto &b : bufs) b = (uint16_t *)heap_caps_aligned_alloc(64, n, MALLOC_CAP_SPIRAM);
  canvas.setColorDepth(16);
  if(bufs[0] && bufs[1])
  {
    memset(bufs[0], 0, n); memset(bufs[1], 0, n);
    canvas.setBuffer(bufs[0], LCD_W, LCD_H, 16);
    frontBuf = bufs[1];
    doneSem = xSemaphoreCreateBinary();
    teSem = xSemaphoreCreateBinary();
    // nucleo 0, acima da tarefa de dados (0) e abaixo do Wi-Fi: quase todo o tempo dormindo
    if(doneSem && teSem) xTaskCreatePinnedToCore(pushTaskFn, "tela", 4096, nullptr, 2, &pushTask, 0);
#if PIN_LCD_TE >= 0
    // por seguranca: se o ultimo reinicio foi por watchdog de interrupcao, liga sem o TE desta vez
    if(esp_reset_reason() != ESP_RST_INT_WDT)
    {
      pinMode(PIN_LCD_TE, INPUT);
      attachInterrupt(PIN_LCD_TE, teIsr, RISING);
    }
    teCheckAt = millis();
#endif
  }
  else
  {
    // sem memoria para dois quadros: modo antigo (um quadro, envio esperando)
    canvas.setPsram(true);
    if(!canvas.createSprite(LCD_W, LCD_H)) return false;
  }
  canvas.fillScreen(TFT_BLACK);
  return true;
}

void displayPush()
{
  const uint32_t t0 = micros();
  if(!bufs[0] || !bufs[1] || !pushTask)
  {
    canvas.pushSprite(0, 0); frontBuf = (uint16_t *)canvas.getBuffer();
    st.pushes++; st.rows += LCD_H; st.px += LCD_W * LCD_H; st.pushUs += micros() - t0; return;
  }
  uint16_t *ready = bufs[drawIdx];
  // linhas que mudaram: de cima para baixo ate a primeira diferenca e de baixo para cima ate a ultima
  int y0 = 0, y1 = LCD_H - 1;
  const bool full = fullNext;
  if(!full)
  {
    const size_t RB = LCD_W * 2;
    while(y0 < LCD_H && !memcmp(ready + y0 * LCD_W, frontBuf + y0 * LCD_W, RB)) y0++;
    if(y0 >= LCD_H) { st.skipped++; st.pushUs += micros() - t0; return; }   // nada mudou: nem usa o fio
    while(y1 > y0 && !memcmp(ready + y1 * LCD_W, frontBuf + y1 * LCD_W, RB)) y1--;
  }
  fullNext = false;
  finishDma();                                   // o envio anterior precisa ter terminado (job livre)
  // blocos que mudaram em cada faixa
  static const int TW = 24, NT = LCD_W / TW;
  job.buf = ready; job.n = 0;
  for(int ya = y0; ya <= y1; ya = (ya / BAND + 1) * BAND)
  {
    const int yb = min(y1, (ya / BAND + 1) * BAND - 1);
    int l, r; chordFor(ya, yb, l, r);
    if(r < l) continue;
    const int tl = l / TW, tr = r / TW;
    uint32_t mask = 0;
    const uint32_t all = ((1u << (tr + 1)) - 1) & ~((1u << tl) - 1);
    if(full) mask = all;
    else
      for(int y = ya; y <= yb && mask != all; y++)
      {
        const uint16_t *a = ready + y * LCD_W, *b = frontBuf + y * LCD_W;
        for(int t = tl; t <= tr; t++)
          if(!(mask & (1u << t)) && memcmp(a + t * TW, b + t * TW, TW * 2)) mask |= 1u << t;
      }
    // trechos seguidos (buracos de ate 2 blocos entram juntos: abrir outra janela custa mais que mandar)
    for(int t = tl; t <= tr && job.n < MAX_RECTS; )
    {
      if(!(mask & (1u << t))) { t++; continue; }
      int e = t;
      for(int k = t + 1; k <= tr; k++)
      {
        if(mask & (1u << k)) e = k;
        else if(k - e > 2) break;
      }
      Rect &q = job.r[job.n++];
      q.y0 = ya; q.y1 = yb; q.x0 = max(l, t * TW); q.x1 = min(r, e * TW + TW - 1);
      t = e + 1;
    }
    (void)NT;
  }
  // junta pedacos da faixa de cima com os de baixo quando a largura e parecida (menos janelas = menos espera)
  if(job.n > 1)
  {
    int m = 0;
    for(int i = 0; i < job.n; i++)
    {
      const Rect &c = job.r[i];
      bool merged = false;
      for(int k = m - 1; k >= 0 && job.r[k].y1 >= c.y0 - 1; k--)
      {
        Rect &p = job.r[k];
        if(p.y1 != c.y0 - 1) continue;
        const int ux0 = min(p.x0, c.x0), ux1 = max(p.x1, c.x1);
        const int32_t uni = (int32_t)(ux1 - ux0 + 1) * (c.y1 - p.y0 + 1);
        const int32_t sep = (int32_t)(p.x1 - p.x0 + 1) * (p.y1 - p.y0 + 1) + (int32_t)(c.x1 - c.x0 + 1) * (c.y1 - c.y0 + 1);
        if(uni <= sep + sep / 4 + 2000) { p.x0 = ux0; p.x1 = ux1; p.y1 = c.y1; merged = true; break; }
      }
      if(!merged) job.r[m++] = c;
    }
    job.n = m;
  }
  const int h = y1 - y0 + 1;
  inFlight = true;
  xTaskNotifyGive(pushTask);
  frontBuf = ready;
  drawIdx ^= 1;
  // o outro quadro so difere nessas linhas: copia so elas (algumas telas desenham por cima do anterior)
  memcpy(bufs[drawIdx] + y0 * LCD_W, ready + y0 * LCD_W, h * LCD_W * 2);
  canvas.setBuffer(bufs[drawIdx], LCD_W, LCD_H, 16);
  st.pushes++; st.pushUs += micros() - t0;
}

void displayWait() { finishDma(); }

void displaySetBrightness(uint8_t percent)
{
  if(percent > 100) percent = 100;
  // Curva perceptiva: brilho baixo mais fino, sem apagar de vez em 1%
  uint32_t v = percent ? (uint32_t)(4 + (251UL * percent * percent) / 10000UL) : 0;
  lcd.setBrightness((uint8_t)v);
}

void displaySetRotation(uint8_t r)
{
  finishDma();
  if(lcd.getRotation() != (r & 3)) fullNext = true;
  lcd.setRotation(r & 3);
}

const uint16_t *displayFrame()
{
  // o espelho do painel le o ultimo quadro enviado (completo), nunca o que esta sendo desenhado
  return frontBuf ? frontBuf : (const uint16_t *)canvas.getBuffer();
}
