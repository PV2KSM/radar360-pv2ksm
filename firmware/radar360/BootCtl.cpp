#include "BootCtl.h"
#include "Config.h"
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <Preferences.h>

static RTC_NOINIT_ATTR uint32_t earlyMagic, earlyCount;   // reinicios anormais antes de 60 s ligado
static const uint32_t MAGIC = 0x52454331;                 // "REC1"
static bool pending = false, decided = false, everConnected = false;

bool bootRecoveryAvailable()
{
  return esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, nullptr) != nullptr;
}

void bootToRecovery(const char *why)
{
  const esp_partition_t *rec = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, nullptr);
  if(!rec) return;
  // A recuperacao volta para este firmware quando o usuario pedir (o otadata e apagado ao escolher a "factory")
  const esp_partition_t *run = esp_ota_get_running_partition();
  Preferences p;
  if(p.begin("r360boot", false)) { p.putString("ret", run ? run->label : "app0"); p.putString("why", why ? why : ""); p.end(); }
  Serial.printf("[boot] indo para a recuperacao: %s\n", why ? why : "");
  Serial.flush();
  if(esp_ota_set_boot_partition(rec) == ESP_OK) { delay(100); esp_restart(); }
}

void bootEarly()
{
  const esp_reset_reason_t r = esp_reset_reason();
  const bool abnormal = r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT;
  if(earlyMagic != MAGIC || !abnormal) { earlyMagic = MAGIC; earlyCount = 0; }
  earlyCount++;
  // 6 travamentos seguidos logo ao ligar (o modo seguro, a partir do 3o, nao resolveu): recuperacao
  if(abnormal && earlyCount >= 6) { earlyCount = 0; bootToRecovery("o firmware travou 6 vezes seguidas ao ligar"); }

  esp_ota_img_states_t st;
  const esp_partition_t *run = esp_ota_get_running_partition();
  pending = run && esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY;
  if(pending) Serial.println("[boot] firmware novo em teste: confirma depois de 60 s funcionando com Wi-Fi");
}

void bootLoop(bool wifiConnected)
{
  const uint32_t up = millis();
  if(up > 60000 && earlyCount) earlyCount = 0;   // passou do primeiro minuto: zera a contagem
  if(!pending || decided) return;
  if(wifiConnected) everConnected = true;
  // Funcionou: 60 s ligado com Wi-Fi (ou 10 min sem Wi-Fi configurado / rede fora do ar)
  if((up > 60000 && everConnected) || up > 600000 || (!configHasWifi() && up > 60000))
  {
    decided = true;
    esp_ota_mark_app_valid_cancel_rollback();
    Serial.println("[boot] firmware novo confirmado");
    return;
  }
  // 4 min sem conseguir entrar no Wi-Fi com redes salvas: provavelmente a atualizacao quebrou a rede
  if(configHasWifi() && !everConnected && up > 240000)
  {
    decided = true;
    Serial.println("[boot] firmware novo nao conectou: voltando para o anterior");
    Serial.flush();
    esp_ota_mark_app_invalid_rollback_and_reboot();   // se nao houver anterior valido, segue nesta
    esp_ota_mark_app_valid_cancel_rollback();
  }
}

void bootBeforeRestart()
{
  // Quem pediu o reinicio usou o painel: o firmware novo funciona. Sem isso, reiniciar durante o teste
  // (ex.: importar configuracoes logo apos a gravacao) faria o aparelho voltar para a versao anterior.
  if(pending && !decided && millis() > 15000) { decided = true; esp_ota_mark_app_valid_cancel_rollback(); }
}

void bootConfirm()
{
  // O usuario esta usando esta versao (ex.: iniciou uma nova gravacao pelo painel): confirma como valida
  // AGORA. Sem isso, uma versao ainda "em teste" bloqueia o inicio de uma nova atualizacao (esp_ota_begin).
  if(pending && !decided) { decided = true; esp_ota_mark_app_valid_cancel_rollback(); }
}

const char *bootOtaState()
{
  if(!pending) return "confirmado";
  return decided ? "confirmado" : "em teste";
}
