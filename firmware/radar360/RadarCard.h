#pragma once
// Cartao de temperatura sobre o radar de chuva (estilos em feat.wCardStyle)
#include "Weather.h"

void radarCardDraw(const WxNow &n);
bool radarCardAnimated();        // neon pulsando, bolha com chuva
bool radarCardCoversBottom();    // o credito "RainViewer" sobe para nao ficar por baixo
