#pragma once
// Bandeiras dos paises e pais de um indicativo
#include <Arduino.h>

#define FLAG_W 120
#define FLAG_H 90

bool flagCountryByName(const char *en, char cc[3], char *namePt, size_t n);   // "Brazil" -> "br", "Brasil"
bool flagCountryByCode(const char *cc, char *namePt, size_t n);               // "br" -> "Brasil"
bool flagCountryByCall(const char *call, char cc[3]);                         // "PY2ABC" -> "br"
const uint16_t *flagImage(const char *cc);                                    // 120x90 (formato do canvas) ou nullptr
uint32_t flagPixel(const uint16_t *img, float u, float v);                    // cor RGB888 em (u,v) 0..1
int flagColors(const uint16_t *img, uint32_t *out, int maxN);                 // cores principais
