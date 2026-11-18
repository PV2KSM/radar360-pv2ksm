#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "Board.h"

// Painel Galaxycore GC9B71/GC9B72 360x360 com a sequencia de inicializacao
// do firmware original (61 comandos).
struct Panel_GC9B71 : public lgfx::Panel_GC9xxx
{
  Panel_GC9B71()
  {
    _cfg.panel_width = _cfg.memory_width = LCD_W;
    _cfg.panel_height = _cfg.memory_height = LCD_H;
    _nop_closing = false;   // chips Galaxycore se confundem com NOP no fim da transacao (como o GC9A01)
  }

protected:
  const uint8_t *getInitCommands(uint8_t listno) const override;
};

class LGFX : public lgfx::LGFX_Device
{
  Panel_GC9B71 _panel;
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;

public:
  LGFX();
  void setBusHz(uint32_t hz) { auto c = _bus.config(); c.freq_write = hz; _bus.config(c); }
};

extern LGFX lcd;
extern LGFX_Sprite canvas;        // Quadro inteiro 360x360 em PSRAM (tambem usado pelo espelho web)

bool displayBegin();
void displayPush();              // envia so as linhas que mudaram (em paralelo, por DMA) e passa a desenhar no outro
void displayWait();              // espera o envio terminar
void displayInvalidate();        // proximo envio manda a tela inteira
void displaySetTurbo(bool on);   // barramento a 80 MHz (senao 40 MHz)
uint8_t displayBusMHz();

struct DisplayStats
{
  uint32_t pushes;     // quadros enviados (com alguma mudanca)
  uint32_t skipped;    // quadros iguais ao anterior (nada enviado)
  uint32_t rows;       // linhas enviadas, somadas
  uint32_t waitUs;     // tempo esperando o envio anterior terminar, somado
  uint32_t pushUs;     // tempo dentro de displayPush (comparar + copiar + esperar), somado
  uint32_t px;         // pixels enviados, somados (retangulo que mudou, cortado no circulo)
  uint32_t teWaits;    // envios que esperaram o TE (inicio da varredura do painel)
  uint32_t teHz;       // avisos de TE por segundo (0 = painel nao manda o sinal)
};
const DisplayStats &displayStats();   // contadores desde o boot (a interface tira as medias)
void displaySetBrightness(uint8_t percent);
void displaySetRotation(uint8_t r);  // 0..3
const uint16_t *displayFrame();    // RGB565 (big-endian), LCD_W*LCD_H pixels
