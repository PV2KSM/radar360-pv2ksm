#pragma once
// Ajudantes de desenho compartilhados pelas telas (fontes, textos, mistura de cores).
#include "Display.h"
#include "Fonts.h"

// ---- Paleta ----
static const uint32_t C_BG     = 0x050B0A;
static const uint32_t C_RING   = 0x14302B;
static const uint32_t C_RING2  = 0x2C5A52;
static const uint32_t C_ACCENT = 0x5FFFD2;
static const uint32_t C_TEXT   = 0xE8FFF9;
static const uint32_t C_MUTED  = 0x88AAA2;
static const uint32_t C_WARN   = 0xFFD65A;
static const uint32_t C_BAD    = 0xFF7A7A;
static const uint32_t C_PANEL  = 0x0E1D1B;

static const int CX = LCD_W / 2, CY = LCD_H / 2;

void gfxFont(const uint8_t *font);
void gfxText(const char *s, int x, int y, const uint8_t *font, uint32_t color, textdatum_t datum = middle_center);
void gfxTextFit(const char *s, int x, int y, int maxW, const uint8_t *font, uint32_t color, textdatum_t datum = middle_center);
int  gfxTextWidth(const char *s, const uint8_t *font);
// Quebra o texto em linhas que cabem no circulo; desenha centralizado a partir de y (topo).
// Devolve a altura usada. maxLines limita (ultima linha com reticencias).
int  gfxWrapRound(const char *s, int yTop, const uint8_t *font, uint32_t color, int lineH, int maxLines, int margin = 18, bool draw = true);
int  gfxRoundHalfWidth(int y, int margin = 0);   // meia largura util do circulo na altura y
// Bloco de texto centralizado verticalmente em cy (mede na posicao final). Devolve as linhas usadas;
// com draw=false so mede (needed recebe quantas linhas o texto inteiro precisaria).
int  gfxWrapRoundCentered(const char *s, int cy, const uint8_t *font, uint32_t color, int lineH, int maxLines, int margin, bool draw = true, int *needed = nullptr);

uint16_t gfxBlend(uint16_t bgSwapped, uint32_t rgb, uint8_t alpha);   // pixels do sprite (RGB565 com bytes trocados)
void gfxBlendPixel(int x, int y, uint32_t rgb, uint8_t alpha);
void gfxBlendLine(float x0, float y0, float x1, float y1, uint32_t rgb, uint8_t alpha);   // linha suave com transparencia
void gfxDim(uint8_t keep);   // escurece o quadro todo (keep 0..255)
void gfxBackground();
void gfxWifiBars(int x, int y, int rssi, uint32_t on, uint32_t off);
void gfxStatusLine(const char *msg, uint32_t color);   // mensagem central (carregando, erro...)
const char *gfxAgo(time_t t, char *buf, size_t n);     // "há 5 min"

// ---- Temas (fontes do pacote visual; sem pacote, apontam para as internas) ----
extern const uint8_t *FxClockXL, *FxClockL, *FxMinimal, *FxLight30;   // ClockXL/ClockL/Minimal: so digitos : . - / % °
extern const uint8_t *FxCinzel34, *FxCinzel20, *FxCinzel14, *FxSerif30, *FxSerif22, *FxMono14, *FxMono20;
extern const uint8_t *FxMono30, *FxFell18, *FxFellIt18, *FxFell30;   // Mono30: so digitos
extern bool fxThemed;
extern const uint8_t *FxOsw14, *FxOsw18, *FxOsw34, *FxHead18, *FxInter13, *FxBarlow17, *FxVT22, *FxVT48;   // noticias estilo TV

// Texto com sombra (legivel sobre fotos e ceu); a versao Fit corta com reticencias
void gfxTextShadowFit(const char *s, int x, int y, int maxW, const uint8_t *font, uint32_t color, textdatum_t datum = middle_center);
void gfxTextShadow(const char *s, int x, int y, const uint8_t *font, uint32_t color, textdatum_t datum = middle_center, uint32_t shadow = 0x05080C);
// Versalete espacado: "BRASIL" -> "B R A S I L" (so para rotulos curtos)
void gfxTextSpaced(const char *s, int x, int y, const uint8_t *font, uint32_t color, int gapPx, textdatum_t datum = middle_center);
// Mascaras alfa de 4 bits do pacote (silhuetas, zodiaco, rosa dos ventos): pinta com a cor
void gfxMask(const uint8_t *a, int w, int h, int x, int y, uint32_t color, uint8_t alpha = 255);
void gfxMaskScaled(const uint8_t *a, int w, int h, int cx, int cy, int outW, uint32_t color, uint8_t alpha = 255);
void gfxMaskRotated(const uint8_t *a, int size, int cx, int cy, float deg, uint32_t color, uint8_t alpha = 255);   // quadrada, gira no centro
void gfxStar(float cx, float cy, float r, uint32_t color);   // estrela de 5 pontas
// Anel/arco suavizado com transparencia (angulos em graus, 0 = topo, sentido horario)
void gfxArc(float cx, float cy, float rIn, float rOut, float a0, float a1, uint32_t color, uint8_t alpha = 255, float featherDeg = 0);
void gfxGlow(int cx, int cy, int r, uint32_t color, uint8_t alpha);   // brilho radial suave
void gfxBlendRect(int x, int y, int w, int h, uint32_t color, uint8_t alpha);
void gfxBlendRoundRect(int x, int y, int w, int h, int r, uint32_t color, uint8_t alpha, uint32_t border = 0, uint8_t borderAlpha = 0);
void gfxShadeRows(int y0, int y1, uint32_t color, uint8_t a0, uint8_t a1);   // degrade vertical sobre a imagem
void gfxGradientV(uint32_t top, uint32_t mid, uint32_t bottom);          // fundo em degrade (sem pacote)
// Imagens do pacote visual (JPEG 360x360) com cache decodificado na PSRAM
bool gfxImage(const char *name);
void gfxTrimCaches(bool all);   // solta fundos guardados (pouca PSRAM); so entre quadros                                         // copia para o canvas; false se faltar
const uint16_t *gfxImageBuf(const char *name);
const char *gfxRoman(int n, char *buf, size_t len);
