#pragma once
// Configuracao dos recursos (Voos, Clima, Fotos, Noticias, DMR), guardada em /features.json no LittleFS.
#include <Arduino.h>
#include <ArduinoJson.h>

enum MapStyle : uint8_t { MAP_NONE = 0, MAP_DARK = 1, MAP_LIGHT = 2, MAP_OSM = 3 };
// Os numeros ficam fixos (sao gravados nas configuracoes); os removidos na limpeza da v0.10 viram outro tema ao carregar.
enum ClockStyle : uint8_t { CLK_REAL = 0, CLK_CLASSIC, CLK_RING, CLK_MINIMAL, CLK_COCKPIT, CLK_SIMPLE,
                           CLK_DECO_REMOVED, CLK_POCKET, CLK_RADIO_REMOVED, CLK_AVCLOCK_REMOVED, CLK_ALTIMETER, CLK_ATTITUDE, CLK_HSI, CLK_COUNT };
enum PhotoClock : uint8_t { PCLK_NONE = 0, PCLK_FOOTER, PCLK_ARC_REMOVED, PCLK_GLASS_REMOVED, PCLK_HANDS, PCLK_COUNT };
enum ViewStyle : uint8_t { VIEW_DARK = 0, VIEW_BRIGHT = 1, VIEW_PLAIN = 2, VIEW_AUTO = 3 };   // "Radar escuro", "Mapa claro", sem mapa, claro de dia/escuro a noite

struct FeatCfg
{
  // Localizacao (compartilhada por voos e clima)
  double lat, lon;
  char place[64];
  uint8_t mapSrc;         // 0 CARTO, 1 OpenStreetMap (ambos sem chave)

  // Telas ligadas na sequencia do botao
  bool scrFlights, scrWxRadar, scrWxNow, scrWxHours, scrWxDays, scrPhotos, scrNews, scrDmr, scrHoro, scrMoon, scrPc, scrNetwork, scrQuotes, scrWifi;

  // Relogio
  uint8_t cStyle;         // ClockStyle

  // Voos
  uint8_t fSource;        // 0 adsb.fi, 1 adsb.lol, 2 as duas (alternando e juntando)
  uint16_t fRadiusKm;     // 5..250
  uint8_t fRefresh;       // segundos
  int32_t fMinAlt, fMaxAlt;   // pes
  uint8_t fMax;           // maximo de avioes
  uint8_t fStyle;         // ViewStyle
  bool fSweep, fVectors, fCallsign, fAltitude, fType, fGround, fRings;
  uint8_t fDistU;         // 0 nm, 1 km, 2 mi
  uint8_t fAltU;          // 0 pes, 1 metros
  uint8_t fSpdU;          // 0 nos, 1 km/h, 2 mph
  uint8_t fCenter;        // 0 localizacao, 1 aeroporto
  char fIcao[5];
  uint8_t fIcon;          // 0 silhueta do tipo, 1 seta (original)
  uint8_t fColorBy;       // 0 altitude, 1 categoria, 2 cor unica (fColor)
  uint32_t fColor;        // cor unica dos avioes (RGB)
  uint8_t fSize;          // tamanho dos avioes e textos no mapa: 0 P, 1 M, 2 G, 3 GG
  uint8_t fTypes;         // bits das categorias exibidas (AcCat)

  // Clima
  uint8_t wZoom;          // 4..7 (radar de chuva)
  uint8_t wFrames;        // quadros da animacao
  uint8_t wStyle;         // ViewStyle do fundo do radar de chuva
  bool wOwn;              // local proprio para o clima
  double wLat, wLon;
  char wPlace[64];
  uint16_t wFrameMs;      // tempo de cada quadro
  bool wCards;            // mostrar cartao de temperatura sobre o radar
  uint8_t wHoursStyle;    // 0 curva (original), 1 faixa do ceu, 2 cartoes
  uint8_t wDaysStyle;     // 0 dourado (original), 1 colunas
  uint8_t wCardStyle;     // 0 classico, 1 vidro, 2 neon, 3 arco, 4 aviacao, 5 minimo, 6 termometro, 7 rosa dos ventos, 8 etiqueta, 9 LCD, 10 bolha de ceu
  uint8_t wNowStyle;      // 0 realista, 1 simples

  // Porta-retrato
  uint16_t pSecs;
  bool pShuffle, pFade;
  uint8_t pStyle;         // PhotoClock
  bool pWxPop;            // aviso do clima sobre a foto

  // Noticias
  uint16_t nTopics;       // bits: 0 principais,1 Brasil,2 Mundo,3 Negocios,4 Tecnologia,5 Entretenimento,6 Esportes,7 Ciencia,8 Saude
  char nQuery[48];
  char nUrl[160];
  uint8_t nSecs;
  uint8_t nRefreshMin;
  uint8_t nStyle;         // 0 Aurora, 1 simples, 2 Telejornal (GC), 3 Plantao, 4 Canal 24 h, 5 Teletexto
  bool nUrgent;           // manchete urgente vira plantao automaticamente

  // ID DMR (hotspot Pi-Star / WPSD)
  char dHost[40];
#define FEAT_SCREENS 15   // = SCR_COUNT (Ui.h); conferido em Ui.cpp
  uint8_t order[24];      // ordem das telas (ids de ScreenId); featOrderFix garante que e uma permutacao
  char pcHost[64];        // PC com LibreHardwareMonitor (ip ou nome[:porta], porta 8085 se omitida)
  uint8_t pcStyle;        // 0 RGB gamer, 1 grafico ao vivo, 2 gerenciador de tarefas
  uint8_t pcSecs;         // atualizar a cada N segundos (1..60)
  uint8_t dPollSecs;
  bool dLookup;           // nome e cidade pelo RadioID.net
  uint8_t dStyle;         // 0 Aurora, 1 simples, 2 bandeira de fundo, 3 faixa diagonal, 4 pixel 8-bit, 5 bandeira nas bordas

  // Cotacoes
#define QUOTES_MAX 10
  char qLocal[6];          // moeda local (BRL...)
  char qItems[QUOTES_MAX][8];   // codigos (USD, EUR, BTC...)
  uint8_t qCount;
  uint8_t qStyle;          // 0 rodizio, 1 plaquinha
  uint8_t qSecs;           // rodizio: segundos por item
  uint8_t qRefresh;        // minutos entre atualizacoes

  // Horoscopo
  uint16_t hSigns;        // bits 0 (Aries) .. 11 (Peixes)
  uint8_t hTheme;         // 0 Celestial, 1 Pergaminho, 2 Constelacao
  uint8_t hSecs;          // troca entre signos (0 = so no botao)
  bool hScores;           // amor/trabalho/saude
};

extern FeatCfg feat;
extern volatile uint32_t featGen;   // muda a cada alteracao (recursos refazem mapas/dados)

// Trava das configuracoes (cfg e feat): a tela segura enquanto desenha e o servidor web
// enquanto altera, assim a tela nunca le um texto pela metade.
void stateLock();
void stateUnlock();
struct StateGuard { StateGuard() { stateLock(); } ~StateGuard() { stateUnlock(); } };

void featLoad();
bool featSave();
void featToJson(JsonObject o);
void featFromJson(JsonObjectConst o);
bool featIsDaylight(double lat, double lon);   // sol acima de -4 graus (inclui o crepusculo claro)
uint8_t featViewNow(uint8_t view, double lat, double lon);   // VIEW_AUTO -> VIEW_BRIGHT/VIEW_DARK conforme o sol
uint8_t featMapStyle(uint8_t view);        // ViewStyle -> MapStyle (respeita a fonte do mapa)
bool featMapNeedsDim(uint8_t view);         // OpenStreetMap no estilo escuro: escurecer
void featFlightsCenter(double &lat, double &lon, const char **label);
void featWeatherCenter(double &lat, double &lon, const char **label);
void featOrderFix(int count);   // completa/limpa feat.order para ter cada tela 0..count-1 uma vez
