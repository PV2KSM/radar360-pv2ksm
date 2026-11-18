#include "Horo.h"
#include "Lang.h"
#include "Gfx.h"
#include "Feat.h"
#include "Pack.h"
#include <time.h>
#include <math.h>

struct Pt { float x, y; };
struct Sign
{
  const char *name, *upper, *dates, *element, *star;
  uint8_t starIdx, n, ne;
  Pt p[10];
  uint8_t e[12][2];
};

// Constelacoes simplificadas (caixa 0..1), so para desenhar a figura
static const Sign SIGNS[12] = {
  {"Áries", "ÁRIES", "21 mar – 19 abr", "fogo", "Hamal", 1, 4, 3, {{.12f,.6f},{.42f,.45f},{.6f,.5f},{.88f,.64f}}, {{0,1},{1,2},{2,3}}},
  {"Touro", "TOURO", "20 abr – 20 mai", "terra", "Aldebarã", 2, 7, 6, {{.08f,.18f},{.35f,.42f},{.5f,.52f},{.45f,.64f},{.62f,.62f},{.92f,.34f},{.28f,.8f}}, {{0,1},{1,2},{2,4},{4,5},{2,3},{3,6}}},
  {"Gêmeos", "GÊMEOS", "21 mai – 20 jun", "ar", "Pólux", 4, 8, 7, {{.25f,.12f},{.28f,.38f},{.3f,.64f},{.24f,.9f},{.6f,.1f},{.63f,.36f},{.67f,.62f},{.72f,.88f}}, {{0,1},{1,2},{2,3},{4,5},{5,6},{6,7},{0,4}}},
  {"Câncer", "CÂNCER", "21 jun – 22 jul", "água", "Altarf", 3, 4, 3, {{.5f,.48f},{.44f,.12f},{.22f,.82f},{.78f,.8f}}, {{0,1},{0,2},{0,3}}},
  {"Leão", "LEÃO", "23 jul – 22 ago", "fogo", "Régulo", 3, 9, 9, {{.1f,.4f},{.26f,.24f},{.44f,.28f},{.56f,.48f},{.74f,.36f},{.9f,.56f},{.76f,.76f},{.54f,.7f},{.36f,.82f}}, {{0,1},{1,2},{2,3},{3,4},{4,5},{5,6},{6,7},{7,3},{7,8}}},
  {"Virgem", "VIRGEM", "23 ago – 22 set", "terra", "Espiga", 6, 8, 7, {{.08f,.28f},{.28f,.34f},{.44f,.5f},{.6f,.44f},{.88f,.28f},{.5f,.74f},{.7f,.86f},{.28f,.8f}}, {{0,1},{1,2},{2,3},{3,4},{2,5},{5,6},{5,7}}},
  {"Libra", "LIBRA", "23 set – 22 out", "ar", "Zubeneschamali", 0, 5, 5, {{.5f,.14f},{.24f,.42f},{.76f,.42f},{.18f,.78f},{.82f,.72f}}, {{0,1},{0,2},{1,2},{1,3},{2,4}}},
  {"Escorpião", "ESCORPIÃO", "23 out – 21 nov", "água", "Antares", 2, 10, 10, {{.08f,.16f},{.18f,.3f},{.3f,.36f},{.4f,.46f},{.5f,.6f},{.55f,.76f},{.66f,.86f},{.8f,.86f},{.88f,.76f},{.84f,.64f}}, {{0,1},{1,2},{2,3},{3,4},{4,5},{5,6},{6,7},{7,8},{8,9},{1,0}}},
  {"Sagitário", "SAGITÁRIO", "22 nov – 21 dez", "fogo", "Kaus Australis", 5, 8, 9, {{.2f,.5f},{.36f,.34f},{.56f,.34f},{.72f,.5f},{.56f,.7f},{.36f,.7f},{.46f,.12f},{.88f,.4f}}, {{0,1},{1,2},{2,3},{3,4},{4,5},{5,0},{1,6},{2,6},{3,7}}},
  {"Capricórnio", "CAPRICÓRNIO", "22 dez – 19 jan", "terra", "Deneb Algedi", 2, 5, 5, {{.1f,.28f},{.5f,.4f},{.9f,.24f},{.74f,.62f},{.44f,.76f}}, {{0,1},{1,2},{2,3},{3,4},{4,0}}},
  {"Aquário", "AQUÁRIO", "20 jan – 18 fev", "ar", "Sadalsuud", 1, 8, 7, {{.08f,.3f},{.28f,.24f},{.44f,.4f},{.6f,.34f},{.7f,.56f},{.54f,.76f},{.78f,.82f},{.92f,.62f}}, {{0,1},{1,2},{2,3},{3,4},{4,5},{5,6},{6,7}}},
  {"Peixes", "PEIXES", "19 fev – 20 mar", "água", "Alrescha", 3, 9, 9, {{.08f,.18f},{.2f,.34f},{.34f,.56f},{.5f,.82f},{.64f,.6f},{.78f,.46f},{.94f,.4f},{.86f,.3f},{.74f,.36f}}, {{0,1},{1,2},{2,3},{3,4},{4,5},{5,6},{6,7},{7,8},{8,5}}},
};

static const char *const OPEN[3][24] = {
 {"An unexpected invitation brings good news.","A frank talk opens doors that seemed closed.",
  "The day favors fresh starts and practical choices.","Someone close needs your attention today.",
  "Creative ideas appear when you least expect them.","An old loose end is finally resolved.",
  "Your effort starts being noticed by those who matter.","The Moon asks for a pause and care with emotions.",
  "News at work shakes up your routine.","A reunion brings back good memories.",
  "It is a great time to sort out your finances.","Your intuition is especially sharp.",
  "Small gestures make a big difference in love.","A chance to learn something new comes up.",
  "The stars favor travel and short outings.","Changes at home bring more lightness.",
  "Distant news may change your plans.","Patience will be your best ally today.",
  "The sky favors partnerships and teamwork.","Your energy is high to get plans off the ground.",
  "Not everything needs an answer now: avoid arguments.","A sincere compliment brightens your day.",
  "There are signs of prosperity in personal projects.","Caring for the body brings clarity to the mind."},
 {"Um convite inesperado traz boas notícias.","Uma conversa franca abre portas que pareciam fechadas.",
  "O dia favorece recomeços e decisões práticas.","Alguém próximo precisa da sua atenção hoje.",
  "Ideias criativas aparecem quando você menos espera.","Uma pendência antiga finalmente se resolve.",
  "Seu esforço começa a ser notado por quem importa.","A Lua pede pausa e cuidado com as emoções.",
  "Novidades no trabalho mexem com a sua rotina.","Um reencontro traz lembranças boas.",
  "O momento é ótimo para organizar as finanças.","Sua intuição está especialmente afiada.",
  "Pequenos gestos fazem grande diferença no amor.","Surge uma chance de aprender algo novo.",
  "Os astros favorecem viagens e passeios curtos.","Mudanças em casa trazem mais leveza.",
  "Uma notícia distante pode mudar seus planos.","A paciência será sua melhor aliada hoje.",
  "O céu favorece parcerias e trabalho em equipe.","Sua energia está alta para tirar planos do papel.",
  "Nem tudo precisa de resposta agora: evite discussões.","Um elogio sincero melhora o seu dia.",
  "Há sinais de prosperidade em projetos pessoais.","Cuidar do corpo traz clareza para a mente."},
 {"Una invitación inesperada trae buenas noticias.","Una charla franca abre puertas que parecían cerradas.",
  "El día favorece los nuevos comienzos y las decisiones prácticas.","Alguien cercano necesita tu atención hoy.",
  "Las ideas creativas aparecen cuando menos lo esperas.","Un asunto pendiente por fin se resuelve.",
  "Tu esfuerzo empieza a ser notado por quien importa.","La Luna pide una pausa y cuidado con las emociones.",
  "Novedades en el trabajo alteran tu rutina.","Un reencuentro trae buenos recuerdos.",
  "Es un gran momento para ordenar las finanzas.","Tu intuición está especialmente afilada.",
  "Los pequeños gestos marcan una gran diferencia en el amor.","Surge una oportunidad de aprender algo nuevo.",
  "Los astros favorecen viajes y paseos cortos.","Los cambios en casa traen más ligereza.",
  "Una noticia lejana puede cambiar tus planes.","La paciencia será tu mejor aliada hoy.",
  "El cielo favorece las alianzas y el trabajo en equipo.","Tu energía está alta para concretar planes.",
  "No todo necesita respuesta ahora: evita discusiones.","Un elogio sincero mejora tu día.",
  "Hay señales de prosperidad en proyectos personales.","Cuidar el cuerpo da claridad a la mente."}
};
static const char *const ADVICE[3][20] = {
 {"Trust your intuition in the afternoon.","Listen more than you speak.","Set aside time just for yourself.",
  "Say yes to what feels good.","Write ideas down before they slip away.","Avoid impulse spending.",
  "A walk helps you decide.","Be kind to yourself.","Call someone who misses you.",
  "Finish what you started.","Drink water and sleep early.","Take a little more risk in love.",
  "Ask for help without fear.","Be grateful for small wins.","Organize your schedule early.",
  "Set pride aside.","Value who is by your side.","Use the evening to rest.",
  "Keep your focus on the essentials.","Make room for the new."},
 {"Confie na sua intuição à tarde.","Ouça mais do que fala.","Reserve um tempo só para você.",
  "Diga sim ao que faz bem.","Anote as ideias antes que escapem.","Evite gastos por impulso.",
  "Uma caminhada ajuda a decidir.","Seja gentil consigo mesmo.","Ligue para quem está com saudade.",
  "Termine o que começou.","Beba água e durma cedo.","Arrisque um pouco mais no amor.",
  "Peça ajuda sem receio.","Agradeça as pequenas vitórias.","Organize a agenda logo cedo.",
  "Deixe o orgulho de lado.","Valorize quem está ao seu lado.","Aproveite a noite para descansar.",
  "Mantenha o foco no essencial.","Abra espaço para o novo."},
 {"Confía en tu intuición por la tarde.","Escucha más de lo que hablas.","Reserva un tiempo solo para ti.",
  "Di sí a lo que te hace bien.","Anota las ideas antes de que se escapen.","Evita gastos por impulso.",
  "Una caminata ayuda a decidir.","Sé amable contigo mismo.","Llama a quien te extraña.",
  "Termina lo que empezaste.","Bebe agua y duerme temprano.","Arriésgate un poco más en el amor.",
  "Pide ayuda sin miedo.","Agradece las pequeñas victorias.","Organiza la agenda temprano.",
  "Deja el orgullo de lado.","Valora a quien está a tu lado.","Aprovecha la noche para descansar.",
  "Mantén el foco en lo esencial.","Abre espacio para lo nuevo."}
};
static const char *const COLORS[3][10] = {
 {"gold","blue","green","white","lilac","red","orange","silver","pink","yellow"},
 {"dourado","azul","verde","branco","lilás","vermelho","laranja","prata","rosa","amarelo"},
 {"dorado","azul","verde","blanco","lila","rojo","naranja","plata","rosa","amarillo"}};
static const uint32_t COLOR_RGB[] = {0xF2C66Du, 0x7FB2FFu, 0x7DE0A0u, 0xF2F4F6u, 0xC9A6FFu, 0xFF7A7Au, 0xFFB060u, 0xC8D0DAu, 0xFF9FC4u, 0xFFE070u};
static const char *const MOODS[3][4] = {
 {"High spirits","Calm mood","Creative mood","Intense mood"},
 {"Humor alto","Humor sereno","Humor criativo","Humor intenso"},
 {"Ánimo alto","Ánimo sereno","Ánimo creativo","Ánimo intenso"}};

static const char *const SIGN_NAME[3][12] = {
 {"Aries","Taurus","Gemini","Cancer","Leo","Virgo","Libra","Scorpio","Sagittarius","Capricorn","Aquarius","Pisces"},
 {"Áries","Touro","Gêmeos","Câncer","Leão","Virgem","Libra","Escorpião","Sagitário","Capricórnio","Aquário","Peixes"},
 {"Aries","Tauro","Géminis","Cáncer","Leo","Virgo","Libra","Escorpio","Sagitario","Capricornio","Acuario","Piscis"}};
static const char *const SIGN_UPPER[3][12] = {
 {"ARIES","TAURUS","GEMINI","CANCER","LEO","VIRGO","LIBRA","SCORPIO","SAGITTARIUS","CAPRICORN","AQUARIUS","PISCES"},
 {"ÁRIES","TOURO","GÊMEOS","CÂNCER","LEÃO","VIRGEM","LIBRA","ESCORPIÃO","SAGITÁRIO","CAPRICÓRNIO","AQUÁRIO","PEIXES"},
 {"ARIES","TAURO","GÉMINIS","CÁNCER","LEO","VIRGO","LIBRA","ESCORPIO","SAGITARIO","CAPRICORNIO","ACUARIO","PISCIS"}};
static const char *const SIGN_DATES[3][12] = {
 {"Mar 21 – Apr 19","Apr 20 – May 20","May 21 – Jun 20","Jun 21 – Jul 22","Jul 23 – Aug 22","Aug 23 – Sep 22","Sep 23 – Oct 22","Oct 23 – Nov 21","Nov 22 – Dec 21","Dec 22 – Jan 19","Jan 20 – Feb 18","Feb 19 – Mar 20"},
 {"21 mar – 19 abr","20 abr – 20 mai","21 mai – 20 jun","21 jun – 22 jul","23 jul – 22 ago","23 ago – 22 set","23 set – 22 out","23 out – 21 nov","22 nov – 21 dez","22 dez – 19 jan","20 jan – 18 fev","19 fev – 20 mar"},
 {"21 mar – 19 abr","20 abr – 20 may","21 may – 20 jun","21 jun – 22 jul","23 jul – 22 ago","23 ago – 22 sep","23 sep – 22 oct","23 oct – 21 nov","22 nov – 21 dic","22 dic – 19 ene","20 ene – 18 feb","19 feb – 20 mar"}};
static const char *const SIGN_ELEM[3][12] = {
 {"fire","earth","air","water","fire","earth","air","water","fire","earth","air","water"},
 {"fogo","terra","ar","água","fogo","terra","ar","água","fogo","terra","ar","água"},
 {"fuego","tierra","aire","agua","fuego","tierra","aire","agua","fuego","tierra","aire","agua"}};

const char *horoSignName(int i) { return i >= 0 && i < 12 ? SIGN_NAME[uiLang][i] : "?"; }

static uint32_t mix32(uint32_t x) { x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16; return x; }

struct Reading { char text[200]; int love, work, health, lucky, color, mood; };
static Reading reading(int sign)
{
  const time_t now = time(nullptr); struct tm t; localtime_r(&now, &t);
  const uint32_t day = (t.tm_year + 1900) * 400 + t.tm_yday;
  const uint32_t h = mix32(day * 2654435761u + sign * 40503u + 7);
  Reading r;
  snprintf(r.text, sizeof(r.text), "%s %s", OPEN[uiLang][h % 24], ADVICE[uiLang][(h >> 8) % 20]);
  r.love = 2 + (h >> 13) % 4; r.work = 2 + (h >> 16) % 4; r.health = 2 + (h >> 19) % 4;
  r.lucky = 1 + (h >> 22) % 60; r.color = (h >> 3) % 10; r.mood = (h >> 28) % 4;
  return r;
}

int horoMoonSign()
{
  const double d = time(nullptr) / 86400.0 + 2440587.5 - 2451545.0;
  const double R = M_PI / 180;
  const double L = 218.316 + 13.176396 * d, M = 134.963 + 13.064993 * d;
  double lon = fmod(L + 6.289 * sin(M * R), 360.0); if(lon < 0) lon += 360;
  return (int)(lon / 30) % 12;
}

// ---- Signos escolhidos e troca ----
static int cur = -1;
static uint32_t shownAt = 0;

static int nextSign(int from)
{
  const uint16_t mask = feat.hSigns ? feat.hSigns : 0x0FFF;
  for(int k = 1; k <= 12; k++) { const int i = (from + k + 12) % 12; if(mask & (1 << i)) return i; }
  return 0;
}

void horoOpen()
{
  const uint16_t mask = feat.hSigns ? feat.hSigns : 0x0FFF;
  if(cur < 0 || !(mask & (1 << cur))) cur = nextSign(-1);
  shownAt = millis();
}

bool horoTick()
{
  const uint16_t mask = feat.hSigns ? feat.hSigns : 0x0FFF;
  if(__builtin_popcount(mask) < 2 || !feat.hSecs) return false;
  if(millis() - shownAt >= feat.hSecs * 1000UL) { cur = nextSign(cur); shownAt = millis(); return true; }
  return false;
}

void horoLongPress() { cur = nextSign(cur < 0 ? -1 : cur); shownAt = millis(); }
void horoStep(int d)
{
  const uint16_t mask = feat.hSigns ? feat.hSigns : 0x0FFF;
  int i = cur < 0 ? 0 : cur;
  for(int k = 1; k <= 12; k++) { const int j = ((i + d * k) % 12 + 12) % 12; if(mask & (1 << j)) { cur = j; break; } }
  shownAt = millis();
}

static void stars(int x, int y, int n, uint32_t on, uint32_t off, float r = 6, int gap = 15)
{
  for(int i = 0; i < 5; i++) gfxStar(x + i * gap, y, r, i < n ? on : off);
}

// ---- Celestial (Aurora) ----
static void drawCelestial(int s, const Reading &r)
{
  const uint32_t GOLD = 0xF2C66Du, LAV = 0xB9B4DEu;
  if(!gfxImage("bg_aurora")) gfxGradientV(0x03040Eu, 0x0A0C2Au, 0x141238u);
  gfxArc(CX, CY, 171, 172.4f, 0, 359.99f, GOLD, 170);
  gfxArc(CX, CY, 145.4f, 146.6f, 0, 359.99f, GOLD, 110);
  gfxArc(CX, CY, 147, 171, -15, 15, GOLD, 45);
  for(int i = 0; i < 12; i++)
  {
    const float a = (i * 30 + 15 - 90) * DEG_TO_RAD;
    gfxBlendLine(CX + cosf(a) * 146, CY + sinf(a) * 146, CX + cosf(a) * 171, CY + sinf(a) * 171, GOLD, 110);
  }
  for(int k = 0; k < 12; k++)
  {
    const int i = (s + k) % 12;
    const float a = (k * 30 - 90) * DEG_TO_RAD;
    const int x = CX + cosf(a) * 159, y = CY + sinf(a) * 159;
    gfxMaskScaled(packZodiac(i, false), 40, 40, x, y, k ? 22 : 26, k ? LAV : GOLD, k ? 190 : 255);
  }
  const Sign &g = SIGNS[s];
  const char *up = SIGN_UPPER[uiLang][s];
  if(gfxTextWidth(up, FxCinzel34) <= 228) gfxText(up, CX, 92, FxCinzel34, 0xF6F3FFu);
  else gfxText(up, CX, 92, FxCinzel20, 0xF6F3FFu);
  char b[64];
  snprintf(b, sizeof(b), "%s · %s", SIGN_DATES[uiLang][s], SIGN_ELEM[uiLang][s]);
  gfxText(b, CX, 121, FontSmall, LAV);
  gfxWrapRoundCentered(r.text, 178, FxSerif22, 0xF6F3FFu, 24, 4, 44);
  if(feat.hScores)
  {
    static const char *const L[3][3] = {{"Love","Work","Health"},{"Amor","Trabalho","Saúde"},{"Amor","Trabajo","Salud"}};
    const int v[] = {r.love, r.work, r.health};
    for(int k = 0; k < 3; k++)
    {
      const int y = 238 + k * 19;
      gfxText(L[uiLang][k], CX - 14, y, FontSmall, LAV, middle_right);
      stars(CX + 2, y, v[k], GOLD, 0x3A3470u);
    }
  }
  snprintf(b, sizeof(b), T("MOON IN %s","LUA EM %s","LUNA EN %s"), SIGN_UPPER[uiLang][horoMoonSign()]);
  gfxTextFit(b, CX, 306, 210, FxCinzel14, GOLD);
}

// ---- Pergaminho ----
static void drawParchment(int s, const Reading &r)
{
  const uint32_t INK = 0x3B2410u, INK2 = 0x6B4A26u;
  if(!gfxImage("bg_parchment")) gfxGradientV(0xF3E6C8u, 0xEBD9B3u, 0xC9AE7Cu);
  gfxMask(packZodiac(s, true), 96, 96, CX - 48, 36, 0x5A3A1Au);
  if(!packZodiac(s, true)) gfxText(SIGN_UPPER[uiLang][s], CX, 84, FxCinzel20, INK);
  gfxText(SIGN_NAME[uiLang][s], CX, 150, FxFell30, INK);
  gfxWrapRoundCentered(r.text, 206, FxFellIt18, INK, 22, 4, 48);
  static const char *const WD[3][7] = {{"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"},{"domingo","segunda","terça","quarta","quinta","sexta","sábado"},{"domingo","lunes","martes","miércoles","jueves","viernes","sábado"}};
  static const char *const MO[3][12] = {{"January","February","March","April","May","June","July","August","September","October","November","December"},{"janeiro","fevereiro","março","abril","maio","junho","julho","agosto","setembro","outubro","novembro","dezembro"},{"enero","febrero","marzo","abril","mayo","junio","julio","agosto","septiembre","octubre","noviembre","diciembre"}};
  const time_t now = time(nullptr); struct tm t; localtime_r(&now, &t);
  char b[64]; snprintf(b, sizeof(b), T("— %s, %d %s —","— %s, %d de %s —","— %s, %d de %s —"), WD[uiLang][t.tm_wday], t.tm_mday, MO[uiLang][t.tm_mon]);
  gfxText(b, CX, 268, FxFell18, INK2);
  if(feat.hScores)
  {
    gfxText(T("Love","Amor","Amor"), CX - 74, 294, FxFell18, INK, middle_right);
    stars(CX - 62, 294, r.love, 0x5A3A1Au, 0xC9B48Au, 5, 12);
    snprintf(b, sizeof(b), T("Luck %d","Sorte %d","Suerte %d"), r.lucky);
    gfxText(b, CX + 24, 294, FxFell18, INK, middle_left);
  }
}

// ---- Constelacao ----
static void drawConstellation(int s, const Reading &r)
{
  if(!gfxImage("bg_constel")) gfxGradientV(0x0F1A33u, 0x070B18u, 0x02040Au);
  const Sign &g = SIGNS[s];
  const float X0 = 76, Y0 = 82, W = 208, H = 118;
  auto P = [&](int i, float &x, float &y) { x = X0 + g.p[i].x * W; y = Y0 + g.p[i].y * H; };
  for(int k = 0; k < g.ne; k++)
  {
    float x0, y0, x1, y1; P(g.e[k][0], x0, y0); P(g.e[k][1], x1, y1);
    gfxBlendLine(x0, y0, x1, y1, 0x7FD8FFu, 120);
  }
  const float tw = millis() / 1000.0f;
  for(int i = 0; i < g.n; i++)
  {
    float x, y; P(i, x, y);
    const bool main = i == g.starIdx;
    gfxGlow(x, y, main ? 14 : 9, 0x7FD8FFu, (uint8_t)(70 + 30 * sinf(tw * 1.5f + i)));
    canvas.fillSmoothCircle(x, y, main ? 3 : 2, 0xEAF6FFu);
  }
  float mx, my; P(g.starIdx, mx, my);
  gfxText(g.star, mx + 10, my - 10, FontSmall, 0x7FD8FFu, middle_left);
  gfxTextSpaced(g.upper, CX, 54, FontSmall, 0x7FD8FFu, 6);
  gfxWrapRoundCentered(r.text, 240, FontSmall, 0xEAF6FFu, 21, 3, 40);
  char b[24];
  struct Chip { char t[24]; uint32_t c; } chips[3];
  snprintf(chips[0].t, sizeof(chips[0].t), "Sorte %d", r.lucky); chips[0].c = 0x7FD8FFu;
  strlcpy(chips[1].t, COLORS[uiLang][r.color], sizeof(chips[1].t)); chips[1].t[0] = toupper(chips[1].t[0]); chips[1].c = COLOR_RGB[r.color];
  strlcpy(chips[2].t, MOODS[uiLang][r.mood], sizeof(chips[2].t)); chips[2].c = 0xB8F27Cu;
  int total = 0, w[2];
  for(int k = 0; k < 2; k++) { w[k] = gfxTextWidth(chips[k].t, FontSmall) + 16; total += w[k] + 6; }
  int x = CX - (total - 6) / 2;
  for(int k = 0; k < 2; k++)
  {
    gfxBlendRoundRect(x, 300, w[k], 24, 9, 0x11243Au, 230);
    gfxText(chips[k].t, x + w[k] / 2, 312, FontSmall, chips[k].c);
    x += w[k] + 6;
  }
  (void)b;
}

void horoDraw()
{
  if(cur < 0) horoOpen();
  const Reading r = reading(cur);
  switch(feat.hTheme)
  {
    case 1: drawParchment(cur, r); break;
    case 2: drawConstellation(cur, r); break;
    default: drawCelestial(cur, r); break;
  }
}
