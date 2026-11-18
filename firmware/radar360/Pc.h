#pragma once
// Tela PC: dados do LibreHardwareMonitor (Windows) pela rede local
#include <Arduino.h>

void pcBegin();
void pcWork();          // tarefa de rede
void pcOpen();
void pcClose();
void pcDraw();
bool pcAnimated();
const char *pcStatus();
const char *pcResolved();   // "192.168.0.23:8085" quando achou o PC
