#pragma once
#include <Arduino.h>

#define WIFI_SLOTS 20   // redes salvas (lista vai crescendo no painel)

struct WifiNet
{
  char ssid[33];
  char pass[65];
};

struct Config
{
  WifiNet wifi[WIFI_SLOTS];
  char host[32];          // nome na rede (host.local)
  char webUser[17];
  char webPass[33];
  char cartoKey[80];      // chave (gratis) dos mapas CARTO; vazia = usa OpenStreetMap
  char tz[48];            // fuso no formato POSIX (ex.: <-03>3 = Brasilia)
  char ntp1[64];
  char ntp2[64];
  uint8_t bright;         // brilho de dia, %
  uint8_t nightBright;    // brilho de noite, %
  bool nightOn;
  uint16_t nightStart;    // minutos desde 00:00
  uint16_t nightEnd;
  uint8_t rotation;       // 0..3
  uint16_t cycleSecs;     // troca automatica de tela (0 = desligada)
  bool showSeconds;
  uint16_t cpuMhz;        // 240, 160 ou 80
  bool cpuAuto;           // baixa sozinho com a tela apagada ou parada
  uint8_t lcdTurbo;       // fio da tela: 0 = 40 MHz, 1 = 80 MHz (confirmado), 2 = 80 MHz em teste
  uint8_t lang;           // idioma da interface: 0=EN, 1=PT, 2=ES
};

extern Config cfg;

void configLoad();
void configSave();
void configFactoryReset();     // apaga tudo e volta ao padrao
bool configHasWifi();
bool configDefaultPassword();
void configCompactWifi();      // tira os buracos da lista de redes  // senha do painel ainda e a de fabrica
