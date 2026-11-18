#include "Lang.h"

uint8_t uiLang = LANG_EN;   // padrao de fabrica: ingles

const char *T(const char *en, const char *pt, const char *es)
{
  return uiLang == LANG_PT ? pt : uiLang == LANG_ES ? es : en;
}

const char *langName(uint8_t l)
{
  return l == LANG_PT ? "Portugues" : l == LANG_ES ? "Espanol" : "English";
}
