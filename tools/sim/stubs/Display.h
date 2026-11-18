#pragma once
// Simulador: so o quadro (sprite) em memoria, sem painel
#include <Arduino.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#define LCD_W 360
#define LCD_H 360
extern LGFX_Sprite canvas;
bool displayBegin();
void displayPush();
void displaySetBrightness(uint8_t percent);
void displaySetRotation(uint8_t r);
const uint16_t *displayFrame();
