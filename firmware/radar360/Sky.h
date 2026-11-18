#pragma once
// Ceu realista que acompanha o clima e a hora: escolhe a cena (sol, nuvens, chuva, tempestade,
// neblina, noite, amanhecer, por do sol) e anima por cima (chuva caindo, raios, estrelas, lua).
#include <Arduino.h>

enum SkyScene : uint8_t
{
  SKY_CLEAR_DAY, SKY_PARTLY_DAY, SKY_CLOUDY_DAY, SKY_RAIN_DAY, SKY_STORM, SKY_FOG,
  SKY_CLEAR_NIGHT, SKY_PARTLY_NIGHT, SKY_CLOUDY_NIGHT, SKY_RAIN_NIGHT, SKY_DAWN, SKY_DUSK, SKY_COUNT
};

uint8_t skyScene();
void skyDraw(bool animate = true);   // fundo inteiro no canvas
uint32_t skyFrameMs();               // 0 = parado; senao intervalo sugerido entre quadros
const char *skySceneName(uint8_t s);
float skyMoonAge();
                  // dias desde a lua nova
float skyMoonAlt(time_t t);          // altura da lua (graus) no local configurado
float skyMoonIllum();                // fracao iluminada (0..1)
