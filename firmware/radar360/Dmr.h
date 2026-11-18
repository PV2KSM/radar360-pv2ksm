#pragma once
// ID DMR: quem esta falando no hotspot (Pi-Star / WPSD), lendo a tabela "Last Heard" do dashboard
// pela rede local; nome e cidade pelo RadioID.net. Mesma logica do ATS-Mini PV2KSM.
#include <Arduino.h>

void dmrBegin();
void dmrWork();
void dmrOpen();
void dmrDraw();
bool dmrAnimated();
void dmrLongPress();       // alterna entre destaque e lista
const char *dmrStatus();
bool netResolveHost(const char *host, char *out, size_t n);   // IP, DNS ou nome.local -> "ip[:porta]"
