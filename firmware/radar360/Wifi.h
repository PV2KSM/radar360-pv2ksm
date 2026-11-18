#pragma once
// Tela Wi-Fi: explorador das redes 2.4 GHz (lista + canais). Ver Wifi.cpp.
#include <Arduino.h>

void wifiOpen();
void wifiClose();
void wifiWork();
void wifiDraw();
bool wifiShortPress();     // alterna lista/canais
void wifiScroll(int d);    // rola a lista (▲▼)
bool wifiAnimated();       // true enquanto varre (para redesenhar o "procurando")
