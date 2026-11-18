#include "Net.h"
#include "Config.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <esp_sntp.h>

static NetState state = NET_OFF;
static bool setupOnly = false;
static bool apOn = false;
static DNSServer dns;
static String apName;

// Candidatos da ultima varredura (redes salvas encontradas), do mais forte ao mais fraco
struct Cand { int slot; char ssid[33]; int32_t channel; uint8_t bssid[6]; bool hasBssid; int rssi; };
static Cand cands[WIFI_SLOTS];
static int candCount = 0, candIdx = 0;
static uint32_t stateSince = 0, nextScanAt = 0, lostSince = 0;

static ScanEntry scanList[24];
static int scanHidden = 0;
static uint8_t chanAp[14];
static int8_t chanRssi[14];
static int scanCount = 0;
static bool webScanWanted = false;
static bool scanForWeb = false;

static volatile uint8_t lastReason = 0;
static String lastFailSsid;

static const uint32_t CONNECT_TIMEOUT_MS = 20000;
static const uint32_t RETRY_MS = 20000;          // nova rodada de tentativas
static const uint32_t RETRY_AP_CLIENT_MS = 60000; // mais espacado se alguem esta no ponto de acesso
static const int ROUNDS_BEFORE_AP = 3;            // rodadas sem sucesso antes de abrir o ponto de acesso

// Potencia de transmissao: algumas placas conectam melhor com menos potencia (picos de corrente)
static const wifi_power_t TX_LEVELS[] = {WIFI_POWER_19_5dBm, WIFI_POWER_15dBm, WIFI_POWER_11dBm};
static int txLevel = 0;
static int failedRounds = 0;
static int attemptOnCand = 0;
static bool reconnectTried = false;

static void setState(NetState s) { state = s; stateSince = millis(); }

static void startAp()
{
  if(apOn) return;
  WiFi.mode(setupOnly || !configHasWifi() ? WIFI_AP : WIFI_AP_STA);
  WiFi.softAP(apName.c_str());
  delay(100);
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());
  apOn = true;
}

static void stopAp()
{
  if(!apOn) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apOn = false;
}

static void startScan()
{
  if(WiFi.scanComplete() == WIFI_SCAN_RUNNING) return;
  WiFi.scanDelete();
  WiFi.scanNetworks(true, false);
  setState(NET_SCANNING);
}

static void storeScan(int n)
{
  scanCount = 0; scanHidden = 0;
  memset(chanAp, 0, sizeof(chanAp));
  for(int i = 0; i < 14; i++) chanRssi[i] = -127;
  for(int i = 0; i < n; i++)
  {
    // Histograma de canais: conta TODAS as redes (inclusive repetidas e ocultas)
    const int ch = WiFi.channel(i);
    const int rssi = WiFi.RSSI(i);
    if(ch >= 1 && ch <= 13) { if(chanAp[ch] < 255) chanAp[ch]++; if(rssi > chanRssi[ch]) chanRssi[ch] = rssi; }
    String s = WiFi.SSID(i);
    if(!s.length()) { scanHidden++; continue; }
    if(scanCount >= (int)(sizeof(scanList) / sizeof(scanList[0]))) continue;
    bool dup = false;
    for(int j = 0; j < scanCount; j++) if(!strcmp(scanList[j].ssid, s.c_str())) { dup = true; if(rssi > scanList[j].rssi) { scanList[j].rssi = rssi; scanList[j].channel = ch; } break; }
    if(dup) continue;
    strlcpy(scanList[scanCount].ssid, s.c_str(), sizeof(scanList[scanCount].ssid));
    scanList[scanCount].rssi = rssi;
    scanList[scanCount].channel = ch;
    scanList[scanCount].secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    scanCount++;
  }
  // Ordena por sinal
  for(int a = 0; a < scanCount; a++)
    for(int b = a + 1; b < scanCount; b++)
      if(scanList[b].rssi > scanList[a].rssi) { ScanEntry t = scanList[a]; scanList[a] = scanList[b]; scanList[b] = t; }
}

static void pickCandidates(int n)
{
  candCount = 0; candIdx = 0;
  // 1) Redes salvas vistas na varredura (nome igual, sem diferenciar maiusculas)
  for(int slot = 0; slot < WIFI_SLOTS; slot++)
  {
    if(!cfg.wifi[slot].ssid[0]) continue;
    int best = -1;
    for(int i = 0; i < n; i++)
      if(WiFi.SSID(i).equalsIgnoreCase(cfg.wifi[slot].ssid) && (best < 0 || WiFi.RSSI(i) > WiFi.RSSI(best))) best = i;
    if(best < 0) continue;
    Cand &c = cands[candCount++];
    c.slot = slot; c.channel = WiFi.channel(best); c.rssi = WiFi.RSSI(best); c.hasBssid = true;
    strlcpy(c.ssid, WiFi.SSID(best).c_str(), sizeof(c.ssid));
    memcpy(c.bssid, WiFi.BSSID(best), 6);
  }
  for(int a = 0; a < candCount; a++)
    for(int b = a + 1; b < candCount; b++)
      if(cands[b].rssi > cands[a].rssi) { Cand t = cands[a]; cands[a] = cands[b]; cands[b] = t; }
  // 2) Redes salvas que nao apareceram (podem ser ocultas): tenta pelo nome mesmo assim
  // Com muitas redes salvas, so as 4 primeiras da lista que nao apareceram sao tentadas (cada tentativa demora)
  for(int slot = 0, hidden = 0; slot < WIFI_SLOTS && candCount < WIFI_SLOTS && hidden < 4; slot++)
  {
    if(!cfg.wifi[slot].ssid[0]) continue;
    bool seen = false;
    for(int k = 0; k < candCount; k++) if(cands[k].slot == slot) seen = true;
    if(seen) continue;
    hidden++;
    Cand &c = cands[candCount++];
    c.slot = slot; c.channel = 0; c.rssi = -127; c.hasBssid = false;
    strlcpy(c.ssid, cfg.wifi[slot].ssid, sizeof(c.ssid));
  }
}

static void tryCandidate()
{
  const Cand &c = cands[candIdx];
  WiFi.disconnect(false, false);
  lastReason = 0;
  lastFailSsid = c.ssid;
  if(c.hasBssid && attemptOnCand == 0) WiFi.begin(c.ssid, cfg.wifi[c.slot].pass, c.channel, c.bssid);
  else WiFi.begin(c.ssid, cfg.wifi[c.slot].pass);   // 2a tentativa: deixa o Wi-Fi escolher canal/ponto de acesso
  WiFi.setTxPower(TX_LEVELS[txLevel]);
  Serial.printf("[wifi] tentando %s (tentativa %d, potencia nivel %d)\n", c.ssid, attemptOnCand + 1, txLevel);
  setState(NET_CONNECTING);
}

static void onConnected()
{
  setState(NET_CONNECTED);
  lostSince = 0;
  lastReason = 0;
  failedRounds = 0;
  reconnectTried = false;
  Serial.printf("[wifi] conectado em %s, IP %s, %d dBm\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
  if(!setupOnly) stopAp();
  MDNS.end();
  if(MDNS.begin(cfg.host)) MDNS.addService("http", "tcp", 80);
  netApplyTime();
}

// Nenhuma rede salva conectou nesta rodada: tenta de novo com outra potencia;
// depois de algumas rodadas abre o ponto de acesso (sem parar de tentar)
static void roundFailed(uint32_t now)
{
  failedRounds++;
  txLevel = (txLevel + 1) % (int)(sizeof(TX_LEVELS) / sizeof(TX_LEVELS[0]));
  Serial.printf("[wifi] rodada %d sem sucesso\n", failedRounds);
  if(failedRounds >= ROUNDS_BEFORE_AP || !candCount) startAp();
  setState(NET_AP_ONLY);
  nextScanAt = now + (failedRounds < ROUNDS_BEFORE_AP ? 2000 : RETRY_MS);
}

void netApplyTime()
{
  configTzTime(cfg.tz, cfg.ntp1, cfg.ntp2[0] ? cfg.ntp2 : nullptr);
}

void netBegin(bool setupMode)
{
  setupOnly = setupMode;
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char name[24];
  snprintf(name, sizeof(name), "Radar360-%02X%02X", mac[4], mac[5]);
  apName = name;

  WiFi.persistent(false);
  WiFi.setMinSecurity(WIFI_AUTH_OPEN);   // aceita tambem redes WPA antigas (repetidores)
  WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t info) {
    lastReason = info.wifi_sta_disconnected.reason;
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.setAutoReconnect(false);
  WiFi.setHostname(cfg.host);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  setenv("TZ", cfg.tz, 1);
  tzset();

  if(setupOnly || !configHasWifi())
  {
    startAp();
    setState(NET_AP_ONLY);
    WiFi.scanNetworks(true, false);   // lista pronta para a pagina de configuracao
    scanForWeb = true;
    return;
  }
  startScan();
}

void netTick()
{
  const uint32_t now = millis();
  if(apOn) dns.processNextRequest();

  // Resultado de varredura
  const int n = WiFi.scanComplete();
  if(n >= 0)
  {
    storeScan(n);
    if(state == NET_SCANNING)
    {
      pickCandidates(n);
      attemptOnCand = 0;
      if(candCount) tryCandidate();
      else roundFailed(now);
    }
    WiFi.scanDelete();
    scanForWeb = false;
  }

  if(webScanWanted && WiFi.scanComplete() != WIFI_SCAN_RUNNING && state != NET_CONNECTING)
  {
    webScanWanted = false;
    scanForWeb = true;
    WiFi.scanDelete();
    WiFi.scanNetworks(true, false);
  }

  switch(state)
  {
    case NET_SCANNING:
      // Varredura nao comecou (ou travou): tenta de novo
      if(WiFi.scanComplete() == WIFI_SCAN_FAILED && now - stateSince > 3000) startScan();
      else if(now - stateSince > 20000) { WiFi.scanDelete(); startScan(); }
      break;

    case NET_CONNECTING:
      if(WiFi.status() == WL_CONNECTED) onConnected();
      else if(now - stateSince > CONNECT_TIMEOUT_MS || (lastReason && now - stateSince > 6000 && WiFi.status() == WL_CONNECT_FAILED))
      {
        Serial.printf("[wifi] falhou: motivo %d\n", lastReason);
        if(++attemptOnCand < 2) tryCandidate();                     // mesma rede, mais uma vez
        else if(attemptOnCand = 0, ++candIdx < candCount) tryCandidate();
        else { WiFi.disconnect(false, false); roundFailed(now); }
      }
      break;

    case NET_CONNECTED:
      if(WiFi.status() != WL_CONNECTED)
      {
        if(!lostSince) { lostSince = now; reconnectTried = false; Serial.println("[wifi] conexao perdida"); }
        else if(!reconnectTried && now - lostSince > 3000) { reconnectTried = true; WiFi.reconnect(); }
        else if(now - lostSince > 20000) { lostSince = 0; startScan(); }
      }
      else if(lostSince) { lostSince = 0; reconnectTried = false; MDNS.end(); if(MDNS.begin(cfg.host)) MDNS.addService("http", "tcp", 80); }
      break;

    case NET_AP_ONLY:
      // Com redes salvas, tenta de novo de tempos em tempos (exceto no modo configuracao)
      // Com alguem conectado no ponto de acesso, espera mais entre as tentativas
      if(!setupOnly && configHasWifi() && (int32_t)(now - nextScanAt) >= 0)
      {
        nextScanAt = now + (WiFi.softAPgetStationNum() ? RETRY_AP_CLIENT_MS : RETRY_MS);
        startScan();
      }
      break;

    default:
      break;
  }
}

NetState netState() { return state; }
void netPreferLowPower() { txLevel = 1; }
bool netConnected() { return state == NET_CONNECTED && WiFi.status() == WL_CONNECTED; }
bool netApActive() { return apOn; }
bool netSetupMode() { return setupOnly; }
String netSsid() { return netConnected() ? WiFi.SSID() : String(); }
String netIp() { return netConnected() ? WiFi.localIP().toString() : (apOn ? WiFi.softAPIP().toString() : String()); }
String netApSsid() { return apName; }
int netRssi() { return netConnected() ? WiFi.RSSI() : 0; }
bool netTimeValid() { return time(nullptr) > 1700000000; }

void netRequestScan() { webScanWanted = true; }
bool netScanRunning() { return webScanWanted || WiFi.scanComplete() == WIFI_SCAN_RUNNING; }
int netScanResults(const ScanEntry **out) { *out = scanList; return scanCount; }
int netScanHidden() { return scanHidden; }
int netMyChannel() { return WiFi.status() == WL_CONNECTED ? WiFi.channel() : 0; }
void netChannelLoad(uint8_t nAp[14], int8_t rssiMax[14]) { memcpy(nAp, chanAp, sizeof(chanAp)); memcpy(rssiMax, chanRssi, sizeof(chanRssi)); }

const char *netLastError()
{
  switch(lastReason)
  {
    case 0: return "";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_AUTH_FAIL: case WIFI_REASON_MIC_FAILURE: return "senha incorreta?";
    case WIFI_REASON_NO_AP_FOUND: return "rede não encontrada";
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY: case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD: return "segurança da rede incompatível";
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD: return "sinal fraco demais";
    case WIFI_REASON_ASSOC_FAIL: case WIFI_REASON_CONNECTION_FAIL: return "roteador recusou a conexão";
    case WIFI_REASON_BEACON_TIMEOUT: return "sinal perdido";
    default: return "falha na conexão";
  }
}

int netLastReasonCode() { return lastReason; }
String netLastFailSsid() { return lastFailSsid; }
