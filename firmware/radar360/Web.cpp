#include "Web.h"
#include "Config.h"
#include "Lang.h"
#include "Net.h"
#include "Ui.h"
#include "Pc.h"
#include "Display.h"
#include "Version.h"
#include "BootCtl.h"
#include <esp_rom_crc.h>
#include "WebPage.h"
#include "Feat.h"
#include "Photos.h"
#include "MapImg.h"
#include "Flights.h"
#include "News.h"
#include "Crash.h"
#include "Airports.h"
#include <esp_heap_caps.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include <Update.h>
#include "Pack.h"
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_system.h>

static AsyncWebServer server(80);
static volatile uint32_t rebootAt = 0;
static const char *REALM = "Radar360";

// ---- Autenticacao (Digest) ----
static bool authed(AsyncWebServerRequest *req)
{
  if(req->authenticate(cfg.webUser, cfg.webPass, REALM)) return true;
  req->requestAuthentication(REALM, true);
  return false;
}

static volatile bool rebootToRecovery = false;
static void scheduleReboot(uint32_t ms) { rebootAt = millis() + ms; if(!rebootAt) rebootAt = 1; }

static void sendJson(AsyncWebServerRequest *req, JsonDocument &doc, int code = 200)
{
  String out; serializeJson(doc, out);
  req->send(code, "application/json", out);
}

static void sendOk(AsyncWebServerRequest *req, const char *msg = nullptr, bool reboot = false)
{
  JsonDocument d; d["ok"] = true; if(msg) d["message"] = msg; if(reboot) d["reboot"] = true;
  sendJson(req, d);
}

static void sendErr(AsyncWebServerRequest *req, const char *msg, int code = 400)
{
  JsonDocument d; d["ok"] = false; d["message"] = msg;
  sendJson(req, d, code);
}

static const char *resetReasonText()
{
  switch(esp_reset_reason())
  {
    case ESP_RST_POWERON: return "Ligado na energia";
    case ESP_RST_SW: return "Reinício por software";
    case ESP_RST_PANIC: return "Erro (panic)";
    case ESP_RST_INT_WDT: case ESP_RST_TASK_WDT: case ESP_RST_WDT: return "Watchdog";
    case ESP_RST_BROWNOUT: return "Queda de tensão";
    case ESP_RST_DEEPSLEEP: return "Saiu do sono";
    default: return "Outro";
  }
}

// ---- Status ----
static void handleStatus(AsyncWebServerRequest *req)
{
  if(!authed(req)) return;
  JsonDocument d;
  d["name"] = FW_NAME; d["version"] = FW_VERSION; d["stage"] = FW_STAGE;
  d["build"] = __DATE__ " " __TIME__;
  d["uptime"] = (uint32_t)(millis() / 1000);
  d["heap"] = ESP.getFreeHeap();
  d["heapMin"] = ESP.getMinFreeHeap();
  d["psram"] = ESP.getFreePsram();
  d["psramTotal"] = ESP.getPsramSize();
  d["psramLargest"] = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
  d["fsTotal"] = (uint32_t)LittleFS.totalBytes();
  d["fsUsed"] = (uint32_t)LittleFS.usedBytes();
  d["appSize"] = ESP.getSketchSize();
  d["appSlot"] = ESP.getFreeSketchSpace() + ESP.getSketchSize();
  d["connected"] = netConnected();
  d["ap"] = netApActive();
  d["setup"] = netSetupMode();
  d["ssid"] = netSsid();
  d["apSsid"] = netApSsid();
  d["ip"] = netIp();
  d["rssi"] = netRssi();
  d["wifiError"] = netLastError();
  d["wifiErrorCode"] = netLastReasonCode();
  d["wifiErrorSsid"] = netLastFailSsid();
  d["host"] = String(cfg.host) + ".local";
  d["mac"] = WiFi.macAddress();
  d["timeValid"] = netTimeValid();
  d["epoch"] = (uint32_t)time(nullptr);
  if(netTimeValid())
  {
    time_t now = time(nullptr); struct tm t; localtime_r(&now, &t);
    char buf[32]; strftime(buf, sizeof(buf), "%d/%m/%Y %H:%M:%S", &t); d["time"] = buf;
  }
  d["screen"] = uiCurrent();
  d["screenName"] = uiScreenName(uiCurrent());
  d["screenOn"] = uiScreenOn();
  d["brightness"] = uiBrightnessNow();
  d["resetReason"] = resetReasonText();
  d["lastCrash"] = crashLastSummary();
  d["heapInternal"] = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  d["heapLargest"] = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  d["stackLoop"] = crashStackFreeLoop();
  d["stackWorker"] = crashStackFreeWorker();
  d["defaultPassword"] = configDefaultPassword();
  d["safeMode"] = crashSafeMode();
  d["cpuNow"] = getCpuFrequencyMhz();
  {
    const UiPerf &p = uiPerf();
    JsonObject pf = d["perf"].to<JsonObject>();
    pf["busMHz"] = displayBusMHz();
    pf["fps"] = roundf(p.fps * 10) / 10;
    pf["drawMs"] = roundf(p.drawMs * 10) / 10;
    pf["pushMs"] = roundf(p.pushMs * 10) / 10;
    pf["sentPct"] = (int)lroundf(p.sentPct);
    pf["skipPct"] = (int)lroundf(p.skipPct);
    pf["teHz"] = p.teHz;
    JsonArray sc = pf["screens"].to<JsonArray>();
    for(int i = 0; i < SCR_COUNT && i < 16; i++)
      if(p.screenMs[i] > 0) { JsonObject o = sc.add<JsonObject>(); o["name"] = uiScreenName(i); o["ms"] = roundf(p.screenMs[i] * 10) / 10; }
  }
  d["pack"] = packVersion();
  d["packBytes"] = packBytes();
  d["packNeeded"] = PACK_NEEDED;
  d["packWhy"] = packWhy();
  { const esp_partition_t *ap = packPartition(); d["packSlot"] = ap ? ap->size : 0; }
  d["otaState"] = bootOtaState();
  d["recovery"] = bootRecoveryAvailable();
  JsonArray s = d["screens"].to<JsonArray>();
  for(int k = 0; k < SCR_COUNT; k++) { const int i = feat.order[k]; JsonObject o = s.add<JsonObject>(); o["id"] = i; o["name"] = uiScreenName(i); o["on"] = uiScreenEnabled(i); }
  d["flights"] = flightsCount();
  d["flightsStatus"] = flightsStatus();
  { char fs[96]; d["flightsSrc"] = flightsSources(fs, sizeof(fs)); }
  d["pcStatus"] = pcStatus(); d["pcAddr"] = pcResolved();
  d["news"] = newsCount();
  d["photos"] = photosCount();
  sendJson(req, d);
}

// Estado curto para o /control (consultado a cada 2 s)
static void handleCtl(AsyncWebServerRequest *req)
{
  if(!authed(req)) return;
  JsonDocument d;
  d["screen"] = uiCurrent();
  d["name"] = uiScreenName(uiCurrent());
  d["screenOn"] = uiScreenOn();
  d["bright"] = uiBrightnessNow();
  d["paused"] = uiCyclePaused();
  d["cycle"] = cfg.cycleSecs > 0;
  d["detail"] = flightsInDetail();
  d["flights"] = flightsCount();
  d["host"] = cfg.host;
  JsonArray s = d["screens"].to<JsonArray>();
  for(int k = 0; k < SCR_COUNT; k++)
  {
    const int i = feat.order[k];
    if(!uiScreenEnabled(i)) continue;
    JsonObject o = s.add<JsonObject>(); o["id"] = i; o["name"] = uiScreenName(i); o["on"] = true;
  }
  sendJson(req, d);
}

static void sendBlob(AsyncWebServerRequest *req, const char *type, const uint8_t *data, size_t len, bool gz, const char *cache)
{
  AsyncWebServerResponse *r = req->beginResponse(200, type, data, len);
  if(gz) r->addHeader("Content-Encoding", "gzip");
  r->addHeader("Cache-Control", cache);
  req->send(r);
}

// ---- Configuracao ----
static void handleGetConfig(AsyncWebServerRequest *req)
{
  if(!authed(req)) return;
  JsonDocument d;
  JsonArray w = d["wifi"].to<JsonArray>();
  for(int i = 0; i < WIFI_SLOTS; i++)
  {
    if(!cfg.wifi[i].ssid[0]) continue;
    JsonObject o = w.add<JsonObject>();
    o["i"] = i;                              // posicao atual: o painel devolve para manter a senha
    o["ssid"] = cfg.wifi[i].ssid;
    o["hasPass"] = cfg.wifi[i].pass[0] != 0;
  }
  d["wifiMax"] = WIFI_SLOTS;
  d["host"] = cfg.host;
  {
    const size_t kl = strlen(cfg.cartoKey);
    d["cartoKeySet"] = kl > 0;
    d["cartoKeyEnd"] = kl > 4 ? cfg.cartoKey + kl - 4 : "";   // so o final, como a senha do Wi-Fi
  }
  d["webUser"] = cfg.webUser;
  d["tz"] = cfg.tz;
  d["ntp1"] = cfg.ntp1;
  d["ntp2"] = cfg.ntp2;
  d["bright"] = cfg.bright;
  d["nightBright"] = cfg.nightBright;
  d["nightOn"] = cfg.nightOn;
  d["nightStart"] = cfg.nightStart;
  d["nightEnd"] = cfg.nightEnd;
  d["rotation"] = cfg.rotation;
  d["cycleSecs"] = cfg.cycleSecs;
  d["showSeconds"] = cfg.showSeconds;
  d["cpuMhz"] = cfg.cpuMhz;
  d["cpuAuto"] = cfg.cpuAuto;
  d["lcdTurbo"] = cfg.lcdTurbo;
  d["lang"] = cfg.lang;
  sendJson(req, d);
}

static bool validHost(const char *h)
{
  const size_t n = strlen(h);
  if(n < 1 || n > 31) return false;
  for(size_t i = 0; i < n; i++)
  {
    const char c = h[i];
    if(!(isalnum((unsigned char)c) || (c == '-' && i && i < n - 1))) return false;
  }
  return true;
}

// Lista de redes: cada item {ssid, pass?, i?}. Sem "pass", a senha vem da rede que estava na posicao "i".
// Itens com nome vazio somem; a lista e regravada inteira (pode crescer ate WIFI_SLOTS).
static void applyWifi(JsonArrayConst w, bool &reboot)
{
  static WifiNet next[WIFI_SLOTS];
  memset(next, 0, sizeof(next));
  int n = 0;
  for(JsonVariantConst v : w)
  {
    if(n >= WIFI_SLOTS) break;
    const char *ssid = v["ssid"] | "";
    if(!ssid[0] || strlen(ssid) > 32) continue;
    strlcpy(next[n].ssid, ssid, sizeof(next[n].ssid));
    if(v["pass"].is<const char *>()) { strlcpy(next[n].pass, v["pass"].as<const char *>(), sizeof(next[n].pass)); reboot = true; }
    else
    {
      const int from = v["i"] | -1;
      if(from >= 0 && from < WIFI_SLOTS && !strcmp(cfg.wifi[from].ssid, ssid)) strlcpy(next[n].pass, cfg.wifi[from].pass, sizeof(next[n].pass));
    }
    n++;
  }
  for(int i = 0; i < WIFI_SLOTS; i++) if(strcmp(next[i].ssid, cfg.wifi[i].ssid)) reboot = true;
  memcpy(cfg.wifi, next, sizeof(next));
}

// Aplica os campos presentes; devolve uma mensagem de erro ou nullptr
static const char *applyConfig(JsonObjectConst j, bool &reboot)
{
  if(j["wifi"].is<JsonArrayConst>()) applyWifi(j["wifi"].as<JsonArrayConst>(), reboot);
  if(j["host"].is<const char *>())
  {
    String h = j["host"].as<String>(); h.toLowerCase();
    if(!validHost(h.c_str())) return T("Invalid network name: use letters, numbers and hyphen","Nome na rede inválido: use letras, números e hífen","Nombre de red inválido: usa letras, números y guion");
    if(strcmp(h.c_str(), cfg.host)) { strlcpy(cfg.host, h.c_str(), sizeof(cfg.host)); reboot = true; }
  }
  if(j["tz"].is<const char *>()) strlcpy(cfg.tz, j["tz"], sizeof(cfg.tz));
  if(j["cartoKey"].is<const char *>())   // so muda se foi enviada; "-" apaga
  {
    String k = j["cartoKey"].as<String>(); k.trim();
    if(k == "-") k = "";
    for(size_t i = 0; i < k.length(); i++) if(!isalnum((unsigned char)k[i]) && k[i] != '-' && k[i] != '_') return T("Invalid CARTO key","Chave da CARTO inválida","Clave de CARTO inválida");
    if(strcmp(k.c_str(), cfg.cartoKey)) { strlcpy(cfg.cartoKey, k.c_str(), sizeof(cfg.cartoKey)); mapCacheClearLater(); featGen++; }
  }
  if(j["ntp1"].is<const char *>()) strlcpy(cfg.ntp1, j["ntp1"], sizeof(cfg.ntp1));
  if(j["ntp2"].is<const char *>()) strlcpy(cfg.ntp2, j["ntp2"], sizeof(cfg.ntp2));
  if(!cfg.ntp1[0]) strlcpy(cfg.ntp1, "pool.ntp.org", sizeof(cfg.ntp1));
  if(j["bright"].is<int>()) cfg.bright = constrain(j["bright"].as<int>(), 1, 100);
  if(j["nightBright"].is<int>()) cfg.nightBright = constrain(j["nightBright"].as<int>(), 0, 100);
  if(j["nightOn"].is<bool>()) cfg.nightOn = j["nightOn"];
  if(j["nightStart"].is<int>()) cfg.nightStart = constrain(j["nightStart"].as<int>(), 0, 1439);
  if(j["nightEnd"].is<int>()) cfg.nightEnd = constrain(j["nightEnd"].as<int>(), 0, 1439);
  if(j["rotation"].is<int>()) cfg.rotation = j["rotation"].as<int>() & 3;
  if(j["cycleSecs"].is<int>()) cfg.cycleSecs = constrain(j["cycleSecs"].as<int>(), 0, 3600);
  if(j["showSeconds"].is<bool>()) cfg.showSeconds = j["showSeconds"];
  if(j["cpuMhz"].is<int>()) { const int m = j["cpuMhz"]; cfg.cpuMhz = (m == 80 || m == 160) ? m : 240; }
  if(j["cpuAuto"].is<bool>()) cfg.cpuAuto = j["cpuAuto"];
  if(j["lcdTurbo"].is<bool>())
  {
    const bool on = j["lcdTurbo"];
    if(!on) cfg.lcdTurbo = 0;
    else if(cfg.lcdTurbo == 0) uiStartTurboTest();   // liga em teste: precisa confirmar no botao
  }
  if(j["lang"].is<int>())
  {
    const int l = j["lang"]; if(l >= 0 && l <= 2) { cfg.lang = (uint8_t)l; uiLang = cfg.lang; }
  }
  return nullptr;
}

static void handlePostConfig(AsyncWebServerRequest *req, JsonVariant &json)
{
  if(!authed(req)) return;
  StateGuard guard;
  JsonObjectConst j = json.as<JsonObjectConst>();
  bool reboot = false;
  const char *err = applyConfig(j, reboot);
  if(err) { configLoad(); return sendErr(req, err); }   // desfaz o que ja tinha mudado

  configSave();
  netApplyTime();
  uiConfigChanged();
  if(reboot && !j["noReboot"].as<bool>()) scheduleReboot(1500);
  sendOk(req, reboot ? T("Saved. Restarting to apply the network…","Salvo. Reiniciando para aplicar a rede…","Guardado. Reiniciando para aplicar la red…") : "Configurações salvas", reboot);
}

static void handlePassword(AsyncWebServerRequest *req, JsonVariant &json)
{
  if(!authed(req)) return;
  StateGuard guard;
  const char *user = json["user"] | "";
  const char *pass = json["pass"] | "";
  if(strlen(user) < 1 || strlen(user) > 16) return sendErr(req, "Usuário deve ter de 1 a 16 caracteres");
  if(strlen(pass) < 6 || strlen(pass) > 32) return sendErr(req, T("Password must be 6 to 32 characters","Senha deve ter de 6 a 32 caracteres","La contraseña debe tener de 6 a 32 caracteres"));
  strlcpy(cfg.webUser, user, sizeof(cfg.webUser));
  strlcpy(cfg.webPass, pass, sizeof(cfg.webPass));
  configSave();
  sendOk(req, T("Password changed. Log in again with the new credentials.","Senha alterada. Entre de novo com os novos dados.","Contraseña cambiada. Entra de nuevo con los nuevos datos."));
}

// ---- Copia de seguranca das configuracoes ----
// Inclui senhas (Wi-Fi, painel, chave da CARTO): so com login, e o painel avisa para guardar bem o arquivo.
static void handleExport(AsyncWebServerRequest *req)
{
  if(!authed(req)) return;
  JsonDocument d;
  d["r360backup"] = 1;
  d["firmware"] = FW_VERSION;
  JsonObject c = d["config"].to<JsonObject>();
  JsonArray w = c["wifi"].to<JsonArray>();
  for(int i = 0; i < WIFI_SLOTS; i++)
    if(cfg.wifi[i].ssid[0]) { JsonObject o = w.add<JsonObject>(); o["ssid"] = cfg.wifi[i].ssid; o["pass"] = cfg.wifi[i].pass; }
  c["host"] = cfg.host; c["webUser"] = cfg.webUser; c["webPass"] = cfg.webPass; c["cartoKey"] = cfg.cartoKey;
  c["tz"] = cfg.tz; c["ntp1"] = cfg.ntp1; c["ntp2"] = cfg.ntp2;
  c["bright"] = cfg.bright; c["nightBright"] = cfg.nightBright; c["nightOn"] = cfg.nightOn; c["nightStart"] = cfg.nightStart; c["nightEnd"] = cfg.nightEnd;
  c["rotation"] = cfg.rotation; c["cycleSecs"] = cfg.cycleSecs; c["showSeconds"] = cfg.showSeconds; c["cpuMhz"] = cfg.cpuMhz; c["cpuAuto"] = cfg.cpuAuto;
  featToJson(d["features"].to<JsonObject>());
  String out; serializeJsonPretty(d, out);
  AsyncWebServerResponse *r = req->beginResponse(200, "application/json", out);
  r->addHeader("Content-Disposition", "attachment; filename=\"radar360-config.json\"");
  r->addHeader("Cache-Control", "no-store");
  req->send(r);
}

static void handleImport(AsyncWebServerRequest *req, JsonVariant &json)
{
  if(!authed(req)) return;
  StateGuard guard;
  if((json["r360backup"] | 0) != 1) return sendErr(req, T("This file is not a Radar 360 settings backup","Esse arquivo não é uma cópia de configurações do Radar 360","Este archivo no es una copia de configuración del Radar 360"));
  JsonObjectConst c = json["config"];
  bool reboot = true;
  if(!c.isNull())
  {
    const char *err = applyConfig(c, reboot);
    if(err) { configLoad(); return sendErr(req, err); }
    const char *u = c["webUser"] | "", *p = c["webPass"] | "";
    if(strlen(u) >= 1 && strlen(u) <= 16 && strlen(p) >= 6 && strlen(p) <= 32) { strlcpy(cfg.webUser, u, sizeof(cfg.webUser)); strlcpy(cfg.webPass, p, sizeof(cfg.webPass)); }
    configSave();
  }
  JsonObjectConst f = json["features"];
  if(!f.isNull()) { featFromJson(f); featSave(); }
  scheduleReboot(2000);
  sendOk(req, T("Settings restored. Restarting…","Configurações restauradas. Reiniciando…","Configuración restaurada. Reiniciando…"), true);
}

// ---- Wi-Fi ----
static void handleScanGet(AsyncWebServerRequest *req)
{
  if(!authed(req)) return;
  JsonDocument d;
  d["running"] = netScanRunning();
  const ScanEntry *list; const int n = netScanResults(&list);
  JsonArray a = d["nets"].to<JsonArray>();
  for(int i = 0; i < n; i++) { JsonObject o = a.add<JsonObject>(); o["ssid"] = list[i].ssid; o["rssi"] = list[i].rssi; o["secure"] = list[i].secure; }
  sendJson(req, d);
}


// ---- Controle remoto ----
static void handleButton(AsyncWebServerRequest *req, JsonVariant &json)
{
  if(!authed(req)) return;
  const char *t = json["t"] | "short";
  static const struct { const char *n; ButtonEvent e; } M[] = {{"long", BTN_LONG}, {"double", BTN_DOUBLE}, {"prev", BTN_PREV}, {"next", BTN_NEXT}, {"ok", BTN_OK},
                                                             {"up", BTN_UP}, {"down", BTN_DOWN}, {"home", BTN_HOME}, {"pause", BTN_PAUSE}, {"power", BTN_POWER}};
  ButtonEvent e = BTN_SHORT;
  for(const auto &m : M) if(!strcmp(t, m.n)) e = m.e;
  uiPostButton(e);
  sendOk(req);
}

static void handleGoto(AsyncWebServerRequest *req, JsonVariant &json)
{
  if(!authed(req)) return;
  uiPostGoto(json["s"] | 0);
  sendOk(req);
}

static void handlePower(AsyncWebServerRequest *req, JsonVariant &json)
{
  if(!authed(req)) return;
  uiSetScreenOn(json["on"] | true);
  sendOk(req);
}

static void handleBrightness(AsyncWebServerRequest *req, JsonVariant &json)
{
  if(!authed(req)) return;
  StateGuard guard;
  cfg.bright = constrain((int)(json["v"] | 80), 1, 100);
  if(json["save"] | false) configSave();
  uiConfigChanged();
  sendOk(req);
}

static void handleMessage(AsyncWebServerRequest *req, JsonVariant &json)
{
  if(!authed(req)) return;
  uiPostMessage(json["text"] | "Olá!", 4000);
  sendOk(req);
}

static void handleScreen(AsyncWebServerRequest *req)
{
  if(!authed(req)) return;
  const size_t total = LCD_W * LCD_H * 2;
  AsyncWebServerResponse *r = req->beginResponse("application/octet-stream", total, [](uint8_t *buf, size_t maxLen, size_t index) -> size_t {
    const size_t total = LCD_W * LCD_H * 2;
    const uint8_t *src = (const uint8_t *)displayFrame();
    if(!src || index >= total) return 0;
    const size_t n = min(maxLen, total - index);
    memcpy(buf, src + index, n);
    return n;
  });
  r->addHeader("Cache-Control", "no-store");
  r->addHeader("X-Width", String(LCD_W));
  r->addHeader("X-Height", String(LCD_H));
  req->send(r);
}

// ---- Atualizacao (OTA pelo navegador) ----
static bool otaAuthed = false;
static bool otaFailed = false;
static bool otaOurs = false;
static char otaError[96];
static size_t otaTotal = 0, otaDone = 0;
static int lastPct = -1;

static void otaUpload(AsyncWebServerRequest *req, const String &filename, size_t index, uint8_t *data, size_t len, bool final)
{
  if(index == 0)
  {
    otaAuthed = req->authenticate(cfg.webUser, cfg.webPass, REALM);
    otaFailed = false; otaOurs = false; otaError[0] = 0; otaDone = 0; lastPct = -1;
    otaTotal = req->contentLength();
    if(!otaAuthed) return;
    // Cabecalho de imagem ESP: 0xE9 e chip_id (bytes 12-13) = 9 (ESP32-S3)
    if(len < 24 || data[0] != 0xE9 || (data[12] | data[13] << 8) != 9)
    {
      otaFailed = true; strlcpy(otaError, T("File is not an ESP32-S3 firmware","Arquivo não é um firmware para ESP32-S3","El archivo no es un firmware para ESP32-S3"), sizeof(otaError));
      return;
    }
    bootConfirm();   // confirma a versao atual antes de gravar: senao, uma versao "em teste" bloqueia o inicio
    if(Update.isRunning()) Update.abort();   // limpa uma gravacao anterior interrompida (senao begin() falha com "No Error")
    if(!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH))
    {
      otaFailed = true; snprintf(otaError, sizeof(otaError), T("Could not start: %s","Não deu para iniciar: %s","No se pudo iniciar: %s"), Update.errorString());
      return;
    }
    uiSetOtaProgress(0);
  }
  if(!otaAuthed || otaFailed) return;

  // Procura o marcador deste firmware (pode cruzar blocos; basta achar uma vez)
  if(!otaOurs && len >= sizeof(FW_MARKER))
    if(memmem(data, len, FW_MARKER, sizeof(FW_MARKER) - 1)) otaOurs = true;

  if(Update.write(data, len) != len)
  {
    otaFailed = true; snprintf(otaError, sizeof(otaError), T("Write error: %s","Erro ao gravar: %s","Error al grabar: %s"), Update.errorString());
    Update.abort(); uiSetOtaProgress(-1);
    return;
  }
  otaDone += len;
  if(otaTotal)
  {
    const int pct = min(99, (int)(otaDone * 100 / otaTotal));
    if(pct != lastPct) { lastPct = pct; uiSetOtaProgress(pct); }
  }
  if(final)
  {
    if(!Update.end(true))
    {
      otaFailed = true; snprintf(otaError, sizeof(otaError), T("Verification failed: %s","Verificação falhou: %s","Verificación fallida: %s"), Update.errorString());
      uiSetOtaProgress(-1);
    }
    else uiSetOtaProgress(100);
  }
}

static void otaDoneHandler(AsyncWebServerRequest *req)
{
  if(!otaAuthed) { req->requestAuthentication(REALM, true); return; }
  if(otaFailed || !Update.isFinished())
  {
    uiSetOtaProgress(-1);
    return sendErr(req, otaError[0] ? otaError : T("Update incomplete","Atualização incompleta","Actualización incompleta"));
  }
  scheduleReboot(1200);
  sendOk(req, otaOurs ? T("Update complete. Restarting…","Atualização concluída. Reiniciando…","Actualización completa. Reiniciando…") : "Gravado (não parece ser um firmware Radar360). Reiniciando…", true);
}

// ---- Pacote visual (particao "assets") ----
// Grava direto na particao, apagando bloco a bloco. O "R360PK01" do inicio so e escrito no fim, depois de
// conferir o CRC: um envio cortado deixa a particao sem pacote (temas com fontes internas), nunca quebrada.
static bool pkAuthed = false, pkFailed = false;
static char pkError[96];
static uint32_t pkDone = 0, pkErased = 0, pkTotal = 0;
static uint8_t pkHead[20];

static void packUpload(AsyncWebServerRequest *req, const String &filename, size_t index, uint8_t *data, size_t len, bool final)
{
  const esp_partition_t *part = packPartition();
  if(index == 0)
  {
    pkAuthed = req->authenticate(cfg.webUser, cfg.webPass, REALM);
    pkFailed = false; pkError[0] = 0; pkDone = 0; pkErased = 0; pkTotal = req->contentLength();
    if(!pkAuthed) return;
    if(!part) { pkFailed = true; strlcpy(pkError, T("This device has no package partition (flash v0.16 over the cable)","Este aparelho não tem a partição do pacote (grave a v0.16 pelo cabo)","Este aparato no tiene la partición del paquete (graba la v0.16 por cable)"), sizeof(pkError)); return; }
    if(len < 20 || memcmp(data, "R360PK01", 8)) { pkFailed = true; strlcpy(pkError, T("File is not a Radar 360 visual package","Arquivo não é um pacote visual do Radar 360","El archivo no es un paquete visual del Radar 360"), sizeof(pkError)); return; }
    uint32_t ver; memcpy(&ver, data + 8, 4);
    if(ver < PACK_NEEDED) { pkFailed = true; snprintf(pkError, sizeof(pkError), T("Package v%lu is old; this firmware needs v%d or newer","Pacote v%lu é antigo; este firmware precisa da v%d ou mais nova","El paquete v%lu es antiguo; este firmware necesita v%d o más nueva"), (unsigned long)ver, PACK_NEEDED); return; }
    memcpy(pkHead, data, 20);
    // tela sai dos temas antes de apagar (espera ate 1,5 s o quadro "Atualizando")
    const uint32_t f0 = uiOtaFrames();
    uiSetOtaProgress(0);
    for(int i = 0; i < 150 && uiOtaFrames() == f0; i++) delay(10);
    packDisable();
  }
  if(!pkAuthed || pkFailed) return;
  if(pkDone + len > part->size) { pkFailed = true; strlcpy(pkError, T("Package larger than the partition","Pacote maior que a partição","El paquete es mayor que la partición"), sizeof(pkError)); uiSetOtaProgress(-1); return; }
  // apaga os blocos de 64 KB que este pedaco vai ocupar
  while(pkErased < pkDone + len)
  {
    const uint32_t blk = min((uint32_t)0x10000, (uint32_t)part->size - pkErased);
    if(esp_partition_erase_range(part, pkErased, blk) != ESP_OK) { pkFailed = true; strlcpy(pkError, T("Error erasing flash","Erro ao apagar a flash","Error al borrar la flash"), sizeof(pkError)); uiSetOtaProgress(-1); return; }
    pkErased += blk;
  }
  const uint8_t *src = data; size_t n = len; uint32_t at = pkDone;
  if(at < 8)   // o marcador fica em branco (0xFF) ate o fim
  {
    const size_t skip = min((size_t)(8 - at), n);
    src += skip; n -= skip; at += skip;
  }
  if(n && esp_partition_write(part, at, src, n) != ESP_OK) { pkFailed = true; strlcpy(pkError, T("Error writing flash","Erro ao gravar na flash","Error al escribir en la flash"), sizeof(pkError)); uiSetOtaProgress(-1); return; }
  pkDone += len;
  if(pkTotal) uiSetOtaProgress(min(99, (int)(pkDone * 100 / pkTotal)));
  if(final)
  {
    // confere o CRC do que ficou gravado
    uint32_t cnt, crc; memcpy(&cnt, pkHead + 12, 4); memcpy(&crc, pkHead + 16, 4);
    const uint32_t head = 20 + cnt * 32;
    if(cnt == 0 || cnt > 256 || head >= pkDone) { pkFailed = true; strlcpy(pkError, "Pacote incompleto", sizeof(pkError)); uiSetOtaProgress(-1); return; }
    uint8_t *buf = (uint8_t *)malloc(4096);
    uint32_t c = 0;
    for(uint32_t o = head; buf && o < pkDone; )
    {
      const uint32_t k = min((uint32_t)4096, pkDone - o);
      if(esp_partition_read(part, o, buf, k) != ESP_OK) { c = ~crc; break; }
      c = esp_rom_crc32_le(c, buf, k);
      o += k;
    }
    free(buf);
    if(c != crc) { pkFailed = true; strlcpy(pkError, T("Corrupt package (CRC mismatch): send again","Pacote corrompido (CRC não confere): envie de novo","Paquete corrupto (CRC no coincide): envíalo de nuevo"), sizeof(pkError)); uiSetOtaProgress(-1); return; }
    if(esp_partition_write(part, 0, pkHead, 8) != ESP_OK) { pkFailed = true; strlcpy(pkError, T("Error writing header","Erro ao gravar o cabeçalho","Error al escribir el encabezado"), sizeof(pkError)); uiSetOtaProgress(-1); return; }
    uiSetOtaProgress(100);
  }
}

static void packDoneHandler(AsyncWebServerRequest *req)
{
  if(!pkAuthed) { req->requestAuthentication(REALM, true); return; }
  if(pkFailed || !pkDone)
  {
    if(pkErased) { scheduleReboot(4000); String m = String(pkError[0] ? pkError : "Envio incompleto") + ". Reiniciando sem pacote; envie de novo."; return sendErr(req, m.c_str()); }
    return sendErr(req, pkError[0] ? pkError : "Envio incompleto");
  }
  scheduleReboot(1200);
  sendOk(req, T("Visual package saved. Restarting…","Pacote visual gravado. Reiniciando…","Paquete visual grabado. Reiniciando…"), true);
}

static void handleRecovery(AsyncWebServerRequest *req)
{
  if(!authed(req)) return;
  if(!bootRecoveryAvailable()) return sendErr(req, T("This device has no recovery mode (flash v0.16 over the cable)","Este aparelho não tem o modo de recuperação (grave a v0.16 pelo cabo)","Este aparato no tiene modo de recuperación (graba la v0.16 por cable)"));
  rebootToRecovery = true;
  scheduleReboot(1000);
  sendOk(req, T("Restarting in recovery mode…","Reiniciando no modo de recuperação…","Reiniciando en modo de recuperación…"), true);
}

// ---- Sistema ----
static void handleReboot(AsyncWebServerRequest *req)
{
  if(!authed(req)) return;
  scheduleReboot(800);
  sendOk(req, T("Restarting…","Reiniciando…","Reiniciando…"), true);
}

static void handleFactory(AsyncWebServerRequest *req, JsonVariant &json)
{
  if(!authed(req)) return;
  if(strcmp(json["confirm"] | "", "RESET")) return sendErr(req, "Confirmação necessária");
  configFactoryReset();
  scheduleReboot(1000);
  sendOk(req, T("Factory reset done. Restarting in setup mode…","Padrão de fábrica restaurado. Reiniciando no modo configuração…","Restablecido de fábrica. Reiniciando en modo configuración…"), true);
}

static void handleTimeSync(AsyncWebServerRequest *req)
{
  if(!authed(req)) return;
  netApplyTime();
  sendOk(req, netConnected() ? "Sincronizando a hora…" : "Sem internet para sincronizar");
}

// ---- Recursos ----
static void handleGetFeatures(AsyncWebServerRequest *req)
{
  if(!authed(req)) return;
  JsonDocument d;
  featToJson(d.to<JsonObject>());
  sendJson(req, d);
}

static void handlePostFeatures(AsyncWebServerRequest *req, JsonVariant &json)
{
  if(!authed(req)) return;
  StateGuard guard;
  featFromJson(json.as<JsonObjectConst>());
  if(json["clearMapCache"] | false) mapCacheClearLater();   // quem limpa e a tarefa de dados (pode estar usando o cache)
  const bool ok = featSave();
  uiConfigChanged();
  ok ? sendOk(req, "Recursos salvos") : sendErr(req, "Não deu para salvar no armazenamento", 500);
}

// ---- Fotos ----
static bool validPhotoName(const char *n)
{
  const size_t L = strlen(n);
  if(L < 5 || L > 40) return false;
  for(size_t i = 0; i < L; i++) { const char c = n[i]; if(!(isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.')) return false; }
  return !strstr(n, "..") && (L > 4 && !strcmp(n + L - 4, ".jpg"));
}

static void handlePhotoList(AsyncWebServerRequest *req)
{
  if(!authed(req)) return;
  JsonDocument d;
  JsonArray a = d["photos"].to<JsonArray>();
  File dir = LittleFS.open("/photos");
  if(dir) for(File f = dir.openNextFile(); f; f = dir.openNextFile()) { JsonObject o = a.add<JsonObject>(); o["name"] = String(f.name()); o["size"] = (uint32_t)f.size(); f.close(); }
  d["free"] = (uint32_t)(LittleFS.totalBytes() - LittleFS.usedBytes());
  d["total"] = (uint32_t)LittleFS.totalBytes();
  sendJson(req, d);
}

static void handlePhotoGet(AsyncWebServerRequest *req)
{
  if(!authed(req)) return;
  const String n = req->hasParam("name") ? req->getParam("name")->value() : String();
  if(!validPhotoName(n.c_str()) || !LittleFS.exists("/photos/" + n)) return req->send(404, "text/plain", "nao encontrada");
  AsyncWebServerResponse *r = req->beginResponse(LittleFS, "/photos/" + n, "image/jpeg");
  r->addHeader("Cache-Control", "max-age=86400");
  req->send(r);
}

static void handlePhotoDelete(AsyncWebServerRequest *req, JsonVariant &json)
{
  if(!authed(req)) return;
  const char *n = json["name"] | "";
  if(!validPhotoName(n)) return sendErr(req, "Nome inválido");
  LittleFS.remove(String("/photos/") + n);
  photosRescan();
  sendOk(req, "Foto apagada");
}

static File upFile;
static bool upOk = false, upAuthed = false;
static char upErr[64];
static String upName;

static void photoUpload(AsyncWebServerRequest *req, const String &filename, size_t index, uint8_t *data, size_t len, bool final)
{
  if(index == 0)
  {
    upAuthed = req->authenticate(cfg.webUser, cfg.webPass, REALM);
    upOk = false; upErr[0] = 0; upName = filename;
    if(!upAuthed) return;
    if(!validPhotoName(filename.c_str())) { strlcpy(upErr, T("Invalid file name","Nome de arquivo inválido","Nombre de archivo inválido"), sizeof(upErr)); return; }
    if(len < 4 || data[0] != 0xFF || data[1] != 0xD8) { strlcpy(upErr, T("File is not JPEG","O arquivo não é JPEG","El archivo no es JPEG"), sizeof(upErr)); return; }
    if(LittleFS.totalBytes() - LittleFS.usedBytes() < 192 * 1024) { strlcpy(upErr, "Armazenamento cheio: apague fotos ou limpe o cache do mapa", sizeof(upErr)); return; }
    if(!LittleFS.exists("/photos")) LittleFS.mkdir("/photos");
    upFile = LittleFS.open("/photos/" + filename, "w");
    if(!upFile) { strlcpy(upErr, T("Could not create the file","Não deu para criar o arquivo","No se pudo crear el archivo"), sizeof(upErr)); return; }
    upOk = true;
  }
  if(!upOk) return;
  if(index + len > 900 * 1024) { upOk = false; strlcpy(upErr, "Foto grande demais", sizeof(upErr)); upFile.close(); LittleFS.remove("/photos/" + upName); return; }
  if(upFile.write(data, len) != len) { upOk = false; strlcpy(upErr, "Erro ao gravar (armazenamento cheio?)", sizeof(upErr)); upFile.close(); LittleFS.remove("/photos/" + upName); return; }
  if(final) { upFile.close(); photosRescan(); }
}

static void photoUploadDone(AsyncWebServerRequest *req)
{
  if(!upAuthed) { req->requestAuthentication(REALM, true); return; }
  if(!upOk) return sendErr(req, upErr[0] ? upErr : "Envio incompleto");
  sendOk(req, "Foto enviada");
}

static void handleCrash(AsyncWebServerRequest *req)
{
  if(!authed(req)) return;
  req->send(200, "application/json", crashJson());
}

static void handleAirport(AsyncWebServerRequest *req)
{
  if(!authed(req)) return;
  String code = req->hasParam("icao") ? req->getParam("icao")->value() : String();
  code.toUpperCase();
  JsonDocument d;
  const AirportRec *a = code.length() == 4 ? airportFind(code.c_str()) : nullptr;
  if(!a) { d["ok"] = false; d["message"] = "Aeroporto não encontrado"; return sendJson(req, d, 404); }
  d["ok"] = true; d["icao"] = String(a->icao).substring(0, 4); d["name"] = a->name; d["lat"] = a->lat; d["lon"] = a->lon;
  sendJson(req, d);
}

static void addJson(const char *uri, ArJsonRequestHandlerFunction fn)
{
  auto *h = new AsyncCallbackJsonWebHandler(uri, fn);
  h->setMethod(HTTP_POST);
  h->setMaxContentLength(8192);
  server.addHandler(h);
}

void webBegin()
{
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    if(!authed(req)) return;
    AsyncWebServerResponse *r = req->beginResponse(200, "text/html; charset=utf-8", WEB_INDEX_GZ, WEB_INDEX_GZ_LEN);
    r->addHeader("Content-Encoding", "gzip");
    r->addHeader("Cache-Control", "no-cache");
    req->send(r);
  });
  // Controle remoto rapido (instalavel na tela de inicio do celular)
  server.on("/control", HTTP_GET, [](AsyncWebServerRequest *req) {
    if(!authed(req)) return;
    sendBlob(req, "text/html; charset=utf-8", WEB_CONTROL_GZ, WEB_CONTROL_GZ_LEN, true, "no-cache");
  });
  // manifesto e icones sem login: o celular busca antes de saber a senha
  server.on("/control.webmanifest", HTTP_GET, [](AsyncWebServerRequest *req) { sendBlob(req, "application/manifest+json", WEB_MANIFEST, WEB_MANIFEST_LEN, false, "max-age=86400"); });
  server.on("/icon.png", HTTP_GET, [](AsyncWebServerRequest *req) { sendBlob(req, "image/png", WEB_ICON192, WEB_ICON192_LEN, false, "max-age=86400"); });
  server.on("/icon512.png", HTTP_GET, [](AsyncWebServerRequest *req) { sendBlob(req, "image/png", WEB_ICON512, WEB_ICON512_LEN, false, "max-age=86400"); });
  server.on("/apple-touch-icon.png", HTTP_GET, [](AsyncWebServerRequest *req) { sendBlob(req, "image/png", WEB_ICON192, WEB_ICON192_LEN, false, "max-age=86400"); });
  server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *req) { sendBlob(req, "image/png", WEB_ICON192, WEB_ICON192_LEN, false, "max-age=86400"); });
  server.on("/api/ctl", HTTP_GET, handleCtl);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/export", HTTP_GET, handleExport);
  server.on("/api/wifi/scan", HTTP_GET, handleScanGet);
  server.on("/api/wifi/scan", HTTP_POST, [](AsyncWebServerRequest *req) { if(!authed(req)) return; netRequestScan(); sendOk(req); });
  server.on("/api/screen", HTTP_GET, handleScreen);
  server.on("/api/reboot", HTTP_POST, handleReboot);
  server.on("/api/time-sync", HTTP_POST, handleTimeSync);
  server.on("/api/ota", HTTP_POST, otaDoneHandler, otaUpload);
  server.on("/api/pack", HTTP_POST, packDoneHandler, packUpload);
  server.on("/api/recovery", HTTP_POST, handleRecovery);
  addJson("/api/config", handlePostConfig);
  addJson("/api/password", handlePassword);
  {
    auto *h = new AsyncCallbackJsonWebHandler("/api/import", handleImport);
    h->setMethod(HTTP_POST);
    h->setMaxContentLength(24576);
    server.addHandler(h);
  }
  addJson("/api/button", handleButton);
  addJson("/api/goto", handleGoto);
  addJson("/api/power", handlePower);
  addJson("/api/brightness", handleBrightness);
  addJson("/api/message", handleMessage);
  addJson("/api/factory-reset", handleFactory);
  server.on("/api/features", HTTP_GET, handleGetFeatures);
  addJson("/api/features", handlePostFeatures);
  server.on("/api/photos", HTTP_GET, handlePhotoList);
  server.on("/api/photo", HTTP_GET, handlePhotoGet);
  server.on("/api/photos", HTTP_POST, photoUploadDone, photoUpload);
  addJson("/api/photo-delete", handlePhotoDelete);
  server.on("/api/crash", HTTP_GET, handleCrash);
  server.on("/api/crash-clear", HTTP_POST, [](AsyncWebServerRequest *req) { if(!authed(req)) return; crashClear(); sendOk(req, "Histórico apagado"); });
  server.on("/api/airport", HTTP_GET, handleAirport);

  // Portal cativo: no modo ponto de acesso, qualquer endereco leva ao painel
  server.onNotFound([](AsyncWebServerRequest *req) {
    if(netApActive() && !netConnected())
    {
      req->redirect("http://192.168.4.1/");
      return;
    }
    req->send(404, "text/plain", "Nao encontrado");
  });
  server.begin();
}

void webTick()
{
  if(rebootAt && (int32_t)(millis() - rebootAt) >= 0)
  {
    delay(100);
    bootBeforeRestart();
    if(rebootToRecovery) bootToRecovery("pedido pelo painel");
    ESP.restart();
  }
}

bool webRebootPending() { return rebootAt != 0; }
