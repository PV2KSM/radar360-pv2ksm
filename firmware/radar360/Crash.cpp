#include "Crash.h"
#include "Version.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <esp_system.h>
#include <esp_core_dump.h>
#include <esp_heap_caps.h>

struct Crumbs
{
  uint32_t magic;
  char worker[28];
  char ui[20];
  uint32_t heapMin, largestMin, psramMin;
  uint32_t uptime;
};
static RTC_NOINIT_ATTR Crumbs crumbs;
static RTC_NOINIT_ATTR uint32_t quickMagic, quickCount;   // reinicios anormais seguidos com pouco tempo ligado
static bool safeMode = false;
static const uint32_t MAGIC = 0x52333630;   // "R360"
static const char *PATH = "/crash.json";
static char lastSummary[120] = "";
static bool brownout = false;
static TaskHandle_t workerTask = nullptr;

void crumbWorker(const char *w) { strlcpy(crumbs.worker, w, sizeof(crumbs.worker)); }
void crumbUi(const char *w) { strlcpy(crumbs.ui, w, sizeof(crumbs.ui)); }
void crashSetWorkerTask(TaskHandle_t t) { workerTask = t; }
uint32_t crashStackFreeLoop()
{
  static TaskHandle_t h = nullptr;
  if(!h) h = xTaskGetHandle("loopTask");
  return h ? uxTaskGetStackHighWaterMark(h) : 0;
}
uint32_t crashStackFreeWorker() { return workerTask ? uxTaskGetStackHighWaterMark(workerTask) : 0; }
const char *crashLastSummary() { return lastSummary; }
bool crashLastWasBrownout() { return brownout; }

bool crashInternalHeapOk(size_t need)
{
  return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) >= need + 16384 &&
         heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) >= need;
}

static const char *reasonText(esp_reset_reason_t r)
{
  switch(r)
  {
    case ESP_RST_PANIC: return "erro (panic)";
    case ESP_RST_INT_WDT: return "watchdog de interrupção";
    case ESP_RST_TASK_WDT: return "watchdog de tarefa";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "queda de tensão (fonte/cabo)";
    default: return nullptr;
  }
}

void crashTick()
{
  static uint32_t last = 0;
  if(millis() - last < 500) return;
  last = millis();
  const uint32_t h = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const uint32_t l = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const uint32_t p = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  if(!crumbs.heapMin || h < crumbs.heapMin) crumbs.heapMin = h;
  if(!crumbs.largestMin || l < crumbs.largestMin) crumbs.largestMin = l;
  if(!crumbs.psramMin || p < crumbs.psramMin) crumbs.psramMin = p;
  crumbs.uptime = millis() / 1000;
  if(crumbs.uptime > 90) quickCount = 0;   // ficou estavel: zera a contagem
}

void crashBegin()
{
  const esp_reset_reason_t r = esp_reset_reason();
  const char *why = reasonText(r);
  brownout = r == ESP_RST_BROWNOUT;
  const bool crumbsOk = crumbs.magic == MAGIC;
  if(why)
  {
    JsonDocument hist;
    File f = LittleFS.open(PATH, "r");
    if(f) { deserializeJson(hist, f); f.close(); }
    if(!hist.is<JsonArray>()) hist.to<JsonArray>();
    JsonArray a = hist.as<JsonArray>();
    while(a.size() >= 6) a.remove(0);
    JsonObject o = a.add<JsonObject>();
    o["version"] = FW_VERSION;
    o["reason"] = why;
    if(crumbsOk)
    {
      o["worker"] = crumbs.worker; o["ui"] = crumbs.ui; o["uptime"] = crumbs.uptime;
      o["heapMin"] = crumbs.heapMin; o["largestMin"] = crumbs.largestMin; o["psramMin"] = crumbs.psramMin;
    }
    char pcs[200] = "";
    esp_core_dump_summary_t *s = (esp_core_dump_summary_t *)calloc(1, sizeof(esp_core_dump_summary_t));
    if(s && esp_core_dump_image_check() == ESP_OK && esp_core_dump_get_summary(s) == ESP_OK)
    {
      o["task"] = s->exc_task;
      char b[16]; snprintf(b, sizeof(b), "0x%08lx", (unsigned long)s->exc_pc); o["pc"] = b;
      o["cause"] = s->ex_info.exc_cause;
      snprintf(b, sizeof(b), "0x%08lx", (unsigned long)s->ex_info.exc_vaddr); o["vaddr"] = b;
      size_t n = 0;
      for(uint32_t i = 0; i < s->exc_bt_info.depth && i < 16; i++)
        n += snprintf(pcs + n, sizeof(pcs) - n, "%s0x%08lx", i ? " " : "", (unsigned long)s->exc_bt_info.bt[i]);
      o["backtrace"] = pcs;
    }
    free(s);
    esp_core_dump_image_erase();
    f = LittleFS.open(PATH, "w");
    if(f) { serializeJson(hist, f); f.close(); }
    snprintf(lastSummary, sizeof(lastSummary), "%s · %s%s%s", why, crumbsOk ? crumbs.worker : "?", crumbsOk && crumbs.ui[0] ? " · tela " : "", crumbsOk ? crumbs.ui : "");
    Serial.printf("[crash] reinicio anormal: %s | dados: %s | tela: %s | pilha: %s\n", why, crumbsOk ? crumbs.worker : "?", crumbsOk ? crumbs.ui : "?", pcs);
  }
  // Conta reinicios anormais que aconteceram no primeiro minuto ligado
  if(quickMagic != MAGIC) { quickMagic = MAGIC; quickCount = 0; }
  if(why && r != ESP_RST_BROWNOUT && (!crumbsOk || crumbs.uptime < 60)) quickCount++;
  else quickCount = 0;
  safeMode = quickCount >= 3;
  if(safeMode) Serial.printf("[crash] %lu reinicios seguidos: modo seguro\n", (unsigned long)quickCount);
  memset(&crumbs, 0, sizeof(crumbs));
  crumbs.magic = MAGIC;
}

bool crashSafeMode() { return safeMode; }

String crashJson()
{
  File f = LittleFS.open(PATH, "r");
  if(!f) return "[]";
  String s = f.readString();
  f.close();
  return s.length() ? s : String("[]");
}

void crashClear()
{
  LittleFS.remove(PATH);
  lastSummary[0] = 0;
}
