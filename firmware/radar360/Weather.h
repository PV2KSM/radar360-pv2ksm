#pragma once
// Clima (Open-Meteo, sem chave): agora, proximas horas, proximos dias, aviso de chuva
// e radar de chuva animado sobre mapa (RainViewer), como no original.
#include <Arduino.h>

struct WxNow
{
  float temp, feels, wind, gusts, pressure, precip;
  int hum, code, windDir, cloud;
  bool day;
  float tmax, tmin;          // de hoje
  int rainProb;              // maxima de hoje
  time_t sunrise, sunset;    // de hoje (hora local em segundos UTC "falsos", como o resto do relogio)
  time_t at;                 // quando foi buscado
};
struct WxHour { time_t t; float temp, mm; int prob, code; bool day; };
struct WxDay { time_t date; int code; float tmax, tmin; int rainProb; float mm; };

void weatherBegin();
void weatherWork();            // tarefa de rede
void weatherDrawNow();
void weatherDrawHours();
void weatherDrawDays();
void weatherDrawRadar();
bool weatherRadarAnimated();
bool weatherRadarCardAnimated();   // cartao com animacao propria (neon, bolha)
bool weatherNowAnimated();
void weatherRadarLongPress();
void weatherOpen();            // entrada animada (agora, horas, dias)
bool weatherIntroRunning();  // mostra/esconde o cartao de temperatura

const char *weatherCondition(int code);
const char *weatherConditionShort(int code);
bool weatherSummary(float *temp, int *code, bool *day);   // para outras telas
bool weatherNow(WxNow *out);
int  weatherHours(WxHour *out, int max);                  // a partir da hora atual
int  weatherDays(WxDay *out, int max);
const char *weatherPlace();
// Aviso curto ("Chuva em 15 min", "Trovoadas às 16h"). Devolve false se nao ha nada a avisar.
bool weatherAlert(char *title, size_t tl, char *sub, size_t sl);
// Icones desenhados (qualquer tamanho)
void weatherIcon(float cx, float cy, float s, int code, bool day, uint32_t bg);
