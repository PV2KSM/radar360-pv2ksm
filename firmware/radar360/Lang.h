#pragma once
// Idiomas da interface. Padrao: ingles. Troca em Recursos (vale na hora).
// Uso: T("Clock", "Relogio", "Reloj")  -> devolve o texto no idioma atual.
#include <stdint.h>

enum { LANG_EN = 0, LANG_PT = 1, LANG_ES = 2 };

extern uint8_t uiLang;

const char *T(const char *en, const char *pt, const char *es);
const char *langName(uint8_t l);     // "English" / "Portugues" / "Espanol"
