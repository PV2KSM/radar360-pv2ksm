#pragma once
// Relogio com clima: realista (ceu acompanha o tempo), classico, anel 24 h, minimal, cockpit e simples.
#include <Arduino.h>

void clockDraw();
uint32_t clockFrameMs();   // 0 = so redesenha a cada segundo
const char *clockStyleName(uint8_t s);
