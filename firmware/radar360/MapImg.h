#pragma once
// Mapa de fundo a partir de tiles raster (Web Mercator), com cache no LittleFS.
// Monta uma imagem grande na resolucao nativa do tile e reduz para 360x360 com media de area,
// assim o mapa fica nitido (so reduz, nunca amplia).
#include "Display.h"
#include "Feat.h"

// Imagem grande reaproveitada (evita fragmentar a PSRAM). Devolva com mapBigRelease.
LGFX_Sprite *mapBigSprite(int n);
void mapBigRelease(LGFX_Sprite *s);
void mapBigTrim();           // solta a imagem grande guardada (pouca PSRAM)

struct MapCanvas
{
  LGFX_Sprite *big = nullptr;
  int n = 0;          // lado da imagem grande (pixels de tile)
  int z = 0;          // zoom usado
  double scale = 1;   // pixels de tile por pixel de tela
  double worldX = 0, worldY = 0;   // canto superior esquerdo, em pixels do mundo no zoom z
  ~MapCanvas();
  void free();
};

// metrosPorPixel: escala desejada na tela. forcedZoom >= 0 fixa o zoom (escala = 1 tile px por tela px * fator).
// Devolve false se faltou memoria; tiles que falharem ficam com a cor de fundo (err recebe o motivo).
bool mapPrepare(MapCanvas &mc, double lat, double lon, double metersPerPx, uint8_t style, char *err, size_t errLen, int forcedZoom = -1);
void mapDownsample(const MapCanvas &mc, const uint16_t *src, uint16_t *out);   // src: n*n, out: 360*360 (RGB565 com bytes trocados)
uint32_t mapBgColor(uint8_t style);
double mapMetersPerPixel(double lat, int z);
const char *mapAttribution(uint8_t style);
bool mapBaseLoad(double lat, double lon, double mpp, uint8_t style, uint8_t view, uint16_t *out);   // imagem final do flash
void mapBaseSave(double lat, double lon, double mpp, uint8_t style, uint8_t view, const uint16_t *img);
void mapCacheClear();
void mapCacheClearLater();   // pedido de outra tarefa
void mapCacheWork();         // tarefa de dados: executa o pedido
// Escurece/tinge uma imagem RGB565 (bytes trocados): pixel = pixel*keep/255 + add
void mapTint(uint16_t *buf, uint8_t keep, uint32_t addRgb, size_t count = LCD_W * LCD_H);
