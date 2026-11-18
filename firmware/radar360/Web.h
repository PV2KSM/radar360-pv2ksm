#pragma once
#include <Arduino.h>

void webBegin();
void webTick();          // reinicio pendente, etc.
bool webRebootPending();
