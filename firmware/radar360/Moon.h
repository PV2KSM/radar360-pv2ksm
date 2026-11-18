#pragma once
// Tela da Lua: fase, iluminacao, nascer/por e proximas cheia/nova
#include <Arduino.h>

void moonDraw();
uint32_t moonFrameMs();                 // estrelas cintilando
const char *moonPhaseName(float age);   // "Lua crescente", "Quarto minguante"...
