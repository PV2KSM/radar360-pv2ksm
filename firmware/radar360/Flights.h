#pragma once
#include <stddef.h>
// Voos: radar ADS-B no estilo do original (sonar com varredura), com mapa opcional e detalhes do voo.
#include <Arduino.h>

void flightsBegin();
void flightsWork();             // tarefa de rede: atualiza dados e mapa quando a tela esta aberta
void flightsDraw();             // desenha no canvas
bool flightsAnimated();         // a varredura pede quadros continuos
void flightsOpen();
void flightsLongPress();        // abre/fecha detalhes
bool flightsShortPress();       // no modo detalhes: proximo aviao (true = tratado)
bool flightsInDetail();
void flightsClose();
int  flightsCount();
const char *flightsStatus();
const char *flightsSources(char *b, size_t n);   // "adsb.fi ok · adsb.lol ok"
bool flightsStep(int d);   // nos detalhes: aviao anterior (-1) / proximo (+1)
