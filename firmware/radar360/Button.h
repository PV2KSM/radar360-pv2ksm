#pragma once
#include <Arduino.h>

enum ButtonEvent : uint8_t { BTN_NONE = 0, BTN_SHORT, BTN_LONG, BTN_DOUBLE,
                             BTN_PREV, BTN_NEXT, BTN_OK, BTN_UP, BTN_DOWN, BTN_HOME, BTN_PAUSE, BTN_POWER };   // os ultimos vem do controle remoto do painel

void buttonBegin();
bool buttonHeldAtBoot(uint32_t ms);   // botao pressionado ao ligar (forca modo configuracao)
ButtonEvent buttonPoll();             // chamar no loop; devolve um evento por vez
