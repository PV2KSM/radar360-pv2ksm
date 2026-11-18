#pragma once
#include <Arduino.h>
#include "Button.h"

enum ScreenId : uint8_t
{
  SCR_CLOCK = 0, SCR_FLIGHTS, SCR_WX_RADAR, SCR_WX_NOW, SCR_WX_HOURS, SCR_WX_DAYS, SCR_PHOTOS, SCR_NEWS, SCR_DMR, SCR_HORO, SCR_MOON, SCR_PC, SCR_NETWORK, SCR_QUOTES, SCR_WIFI,
  SCR_COUNT
};

void uiBegin();
void uiSplash();
void uiTick();
uint16_t uiWantedCpuMhz();   // processador: configurado, ou menos com a tela apagada/parada (modo automatico)                          // chamado no loop principal (desenha quando precisa)

// Seguros para chamar de outras tarefas (servidor web): so marcam pedidos
void uiPostButton(ButtonEvent e);
void uiPostGoto(uint8_t screen);
void uiPostMessage(const char *text, uint32_t ms);
void uiSetScreenOn(bool on);
void uiSetOtaProgress(int percent);   // -1 = sem atualizacao em andamento
uint32_t uiOtaFrames();         // quadros "Atualizando" ja desenhados (o envio do pacote espera)
void uiStartTurboTest();                // liga o fio da tela a 80 MHz em teste (confirmar no botao)

struct UiPerf
{
  float fps;          // quadros enviados por segundo (ultimos ~2 s)
  float drawMs;       // tempo medio desenhando o quadro da tela atual
  float pushMs;       // tempo medio dentro do envio (comparar linhas, esperar o anterior)
  float sentPct;      // % da tela enviada por quadro (so o que mudou)
  float skipPct;      // % dos quadros iguais ao anterior (nada enviado)
  uint32_t teHz;      // sinal TE do painel por segundo (0 = sem sinal: envia sem sincronizar)
  float screenMs[16]; // desenho medio por tela (0 = ainda nao medida)
};
const UiPerf &uiPerf();
void uiConfigChanged();                 // brilho/rotacao/fuso/recursos mudaram

bool uiScreenOn();
bool uiCyclePaused();
uint8_t uiCurrent();
bool uiScreenEnabled(uint8_t id);
const char *uiScreenName(uint8_t id);
uint8_t uiBrightnessNow();
