#pragma once
// Controle de inicializacao: modo de recuperacao (particao "recovery") e confirmacao de firmware novo.
#include <Arduino.h>

bool bootRecoveryAvailable();
void bootToRecovery(const char *why);   // nao volta: reinicia no modo de recuperacao
void bootEarly();                        // inicio do setup: conta reinicios anormais seguidos
void bootLoop(bool wifiConnected);       // confirma o firmware novo / volta para o anterior se nao funcionar
const char *bootOtaState();
void bootBeforeRestart();                // reinicio pedido (painel): firmware que respondeu esta ok
void bootConfirm();                      // confirma JA a versao atual (ex.: antes de aceitar uma nova gravacao)
