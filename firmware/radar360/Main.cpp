#include "BootCtl.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>
#include "Board.h"
#include "Version.h"
#include "Config.h"
#include "Feat.h"
#include "Display.h"
#include "Button.h"
#include "Net.h"
#include "LanScan.h"
#include "Ui.h"
#include "Http.h"
#include "Web.h"
#include "Flights.h"
#include "Weather.h"
#include "Photos.h"
#include "News.h"
#include "Dmr.h"
#include "Pc.h"
#include "Quotes.h"
#include "Wifi.h"
#include "Crash.h"
#include "MapImg.h"
#include "Gfx.h"
#include <esp_heap_caps.h>
#include "Pack.h"

// As fontes grandes (relogio de 104-118 px) desenham cada letra num buffer temporario na pilha
// (LovyanGFX usa alloca); com a pilha padrao de 8 KB o relogio Minimal estourava e reiniciava.
SET_LOOP_TASK_STACK_SIZE(24 * 1024);

static bool fsOk = false;

// Tarefa de rede (nucleo 0): busca dados dos recursos sem travar a tela
static void workerTask(void *)
{
  for(;;)
  {
    mapCacheWork();
    flightsWork();
    weatherWork();
    newsWork();
    dmrWork();
    pcWork();
    quotesWork();
    wifiWork();
    lanWork();
    crumbWorker("ocioso");
    vTaskDelay(pdMS_TO_TICKS(150));
  }
}

void setup()
{
  tlsUsePsram();   // antes de qualquer conexao segura
  Serial.begin(115200);
  bootEarly();     // travou 6x seguidas ao ligar: vai para a recuperacao
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  buttonBegin();

  // Segurar o botao ao ligar (0,6 s) abre o modo configuracao
  const bool setupMode = buttonHeldAtBoot(600);
  // Continuar segurando ate ~5 s abre o modo de recuperacao (enviar firmware/pacote pelo navegador)
  if(setupMode && buttonHeldAtBoot(4400)) bootToRecovery("botão segurado ao ligar");

  configLoad();
  packBegin();          // fundos, fontes e silhuetas embutidos (antes da abertura)
  displayBegin();
  uiBegin();
  uiSplash();

  fsOk = LittleFS.begin(true, "/littlefs", 10, "littlefs");
  Serial.printf("%s %s (%s) - LittleFS %s\n", FW_NAME, FW_VERSION, FW_STAGE, fsOk ? "ok" : "falhou");
  if(fsOk && LittleFS.exists("/sky"))   // fundos com foto (recurso removido): libera o espaco
  {
    File d = LittleFS.open("/sky");
    for(File f = d.openNextFile(); f; f = d.openNextFile()) { String p = String("/sky/") + f.name(); f.close(); LittleFS.remove(p); }
    d.close();
    LittleFS.rmdir("/sky");
  }
  crashBegin();
  featLoad();
  if(cfg.lcdTurbo == 1 && !crashSafeMode()) displaySetTurbo(true);   // tela a 80 MHz (ja confirmada no botao)

  if(crashSafeMode())
  {
    // Algo esta derrubando o aparelho logo ao ligar: volta para o relogio simples (sem salvar)
    feat.cStyle = CLK_SIMPLE; feat.wNowStyle = 1; feat.pStyle = PCLK_NONE;
    feat.nStyle = 1; feat.dStyle = 1; cfg.cycleSecs = 0;
  }

  flightsBegin();
  weatherBegin();
  photosBegin();
  newsBegin();
  dmrBegin();
  pcBegin();
  quotesBegin();

  if(crashLastWasBrownout()) netPreferLowPower();
  netBegin(setupMode);
  lanBegin();
  webBegin();
  TaskHandle_t wt = nullptr;
  // Prioridade 0 (igual a tarefa ociosa): o sistema reparte o processador e o watchdog nunca dispara
  xTaskCreatePinnedToCore(workerTask, "dados", 32768, nullptr, tskIDLE_PRIORITY, &wt, 0);
  crashSetWorkerTask(wt);

  // Firmware novo: so e confirmado depois de 60 s funcionando com Wi-Fi (bootLoop); se nao, volta ao anterior

  // Watchdog tambem na tarefa da tela: se travar por 12 s, reinicia e registra onde parou
  esp_task_wdt_config_t wdt = {.timeout_ms = 12000, .idle_core_mask = 1, .trigger_panic = true};
  esp_task_wdt_reconfigure(&wdt);
  enableLoopWDT();
  if(crashSafeMode()) uiPostMessage("Modo seguro: reinícios seguidos", 20000);
}

void loop()
{
  const ButtonEvent e = buttonPoll();
  if(e != BTN_NONE) uiPostButton(e);
  {
    StateGuard guard;   // o painel web espera o quadro terminar para mudar configuracoes
    netTick();
    uiTick();
  }
  webTick();
  crashTick();
  // Pouca PSRAM: solta o que e so cache (fundos guardados, mapa grande de reserva) antes de faltar de verdade
  {
    static uint32_t memAt = 0;
    if(millis() - memAt > 2000)
    {
      memAt = millis();
      const size_t freeP = heap_caps_get_free_size(MALLOC_CAP_SPIRAM), bigP = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
      if(freeP < 1500 * 1024 || bigP < 700 * 1024)
      {
        StateGuard guard;
        gfxTrimCaches(freeP < 900 * 1024);
        mapBigTrim();
        Serial.printf("[mem] PSRAM baixa (%u KB livres, maior bloco %u KB): caches soltos\n", (unsigned)(freeP / 1024), (unsigned)(bigP / 1024));
      }
    }
  }
  bootLoop(netConnected());
  // Velocidade do processador (Sistema -> Configuracoes); o Wi-Fi funciona em todas
  static uint32_t cpuCheck = 0;
  if(millis() - cpuCheck > 300)
  {
    cpuCheck = millis();
    const uint16_t want = uiWantedCpuMhz();
    if(getCpuFrequencyMhz() != want) { displayWait(); setCpuFrequencyMhz(want); }
  }
  delay(2);
}
