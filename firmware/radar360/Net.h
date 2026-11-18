#pragma once
#include <Arduino.h>

enum NetState : uint8_t { NET_OFF, NET_SCANNING, NET_CONNECTING, NET_CONNECTED, NET_AP_ONLY };

struct ScanEntry
{
  char ssid[33];
  int8_t rssi;
  uint8_t channel;
  bool secure;
};

void netBegin(bool setupMode);      // setupMode: so ponto de acesso, sem tentar conectar
void netTick();
void netPreferLowPower();             // comeca com menos potencia (apos queda de tensao)
void netApplyTime();                 // reaplica fuso/NTP apos mudar configuracao

NetState netState();
bool netConnected();
bool netApActive();
bool netSetupMode();
String netSsid();
String netIp();
String netApSsid();
int netRssi();
bool netTimeValid();
const char *netLastError();          // motivo da ultima falha de conexao (vazio = nenhuma)
int netLastReasonCode();
String netLastFailSsid();

void netRequestScan();               // varredura pedida pela pagina web (e pela tela Wi-Fi)
bool netScanRunning();
int netScanResults(const ScanEntry **out);
int netScanHidden();                 // redes ocultas (sem nome) na ultima varredura
int netMyChannel();                  // canal da rede conectada (0 se nao conectado)
// Congestionamento por canal 2.4 GHz (indices 1..13): nAp e o nº de redes, rssiMax o sinal mais forte (dBm, -127 vazio)
void netChannelLoad(uint8_t nAp[14], int8_t rssiMax[14]);
