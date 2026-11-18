#pragma once
// Tela Cotacoes: moedas e cripto contra a moeda local (AwesomeAPI, economia.awesomeapi.com.br)
#include <Arduino.h>

void quotesBegin();
void quotesWork();          // tarefa de rede
void quotesOpen();
void quotesClose();
void quotesDraw();
bool quotesTick();          // troca de item no rodizio (true = redesenhar)
const char *quotesStatus();
