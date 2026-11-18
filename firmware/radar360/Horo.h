#pragma once
// Horoscopo do dia: escolha dos signos no painel, tres temas (Celestial, Pergaminho, Constelacao).
// O texto e montado no proprio aparelho (banco de frases em portugues, muda a cada dia e por signo),
// sem depender de servico na internet. A "Lua em ..." e calculada de verdade.
#include <Arduino.h>

void horoOpen();
void horoDraw();
bool horoTick();          // troca de signo automatica (true = redesenhar)
void horoLongPress();     // proximo signo
const char *horoSignName(int i);
int horoMoonSign();       // signo em que a Lua esta agora (0 = Aries)
void horoStep(int d);   // signo anterior (-1) / proximo (+1)
