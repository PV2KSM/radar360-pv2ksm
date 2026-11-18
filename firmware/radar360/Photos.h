#pragma once
// Porta-retrato: fotos JPEG 360x360 em /photos (enviadas pelo navegador), com transicao suave.
#include <Arduino.h>

void photosBegin();
void photosOpen();
void photosDraw();
bool photosAnimated();
void photosLongPress();       // pausa / continua
int  photosCount();
void photosRescan();          // lista mudou (envio/exclusao)
bool photosStep(int d);   // foto anterior (-1) / proxima (+1)
