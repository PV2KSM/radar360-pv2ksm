#pragma once
// Diagnostico de reinicios: guarda o motivo, o ultimo passo de cada tarefa (migalhas em RTC)
// e o resumo do core dump (tarefa, PC e backtrace) em /crash.json, visivel no painel.
#include <Arduino.h>

void crashBegin();                     // chamar depois de montar o LittleFS
void crashTick();                      // atualiza minimos de memoria (loop principal)
void crumbWorker(const char *what);    // ultimo passo da tarefa de dados
void crumbUi(const char *what);        // ultima tela desenhada
String crashJson();                    // historico (JSON)
void crashClear();
const char *crashLastSummary();        // texto curto do ultimo reinicio anormal ("" se nenhum)
bool crashLastWasBrownout();
bool crashSafeMode();                  // 3+ reinicios seguidos logo ao ligar: telas simples, sem animacao
uint32_t crashStackFreeLoop();
uint32_t crashStackFreeWorker();
void crashSetWorkerTask(TaskHandle_t t);
bool crashInternalHeapOk(size_t need); // ha memoria interna para TLS/PNG?
