#pragma once
// Noticias: manchetes de feeds RSS/Atom (Google Noticias Brasil por tema, busca, ou um feed seu).
#include <Arduino.h>

void newsBegin();
void newsWork();
void newsOpen();
void newsDraw();
bool newsTick();
bool newsAnimated();        // letreiro correndo / tarja pulsando            // troca de manchete (true = redesenhar)
void newsLongPress();       // QR code do link
bool newsShortPress();      // com o QR aberto: fecha
int  newsCount();
const char *newsTopicName(int bit);
bool newsStep(int d);   // manchete anterior (-1) / proxima (+1)
