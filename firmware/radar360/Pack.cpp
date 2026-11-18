#include "Pack.h"
#include "Gfx.h"

#ifdef R360_SIM
extern const uint8_t PACK_DATA[];
extern const uint32_t PACK_SIZE;
#else
// No aparelho o pacote fica na particao "assets" (fora do firmware) e e lido direto da flash (mmap)
#include <esp_partition.h>
#include <esp_rom_crc.h>
static const uint8_t *PACK_DATA = nullptr;
static uint32_t PACK_SIZE = 0;
static esp_partition_mmap_handle_t mapHandle = 0;
static bool mapped = false;
#endif

struct Entry { char name[24]; uint32_t off, len; };

static Entry *table = nullptr;     // copia na RAM: continua valida mesmo se a flash for apagada (envio de pacote novo)
static const char *why = "";
static int count = 0;
static uint32_t version = 0;
static bool ok = false;
static const uint8_t *sil30 = nullptr, *sil150 = nullptr;

// Fontes dos temas: sem o pacote, caem nas fontes internas
const uint8_t *FxClockXL = FontClock, *FxClockL = FontClock, *FxMinimal = FontClock, *FxLight30 = FontTitle;
const uint8_t *FxCinzel34 = FontTitle, *FxCinzel20 = FontBody, *FxCinzel14 = FontSmall;
const uint8_t *FxSerif30 = FontTitle, *FxSerif22 = FontBody, *FxMono14 = FontSmall, *FxMono20 = FontBody;
const uint8_t *FxMono30 = FontClock, *FxFell18 = FontSmall, *FxFellIt18 = FontSmall, *FxFell30 = FontTitle;
// Noticias estilo TV
const uint8_t *FxOsw14 = FontSmall, *FxOsw18 = FontBody, *FxOsw34 = FontTitle, *FxHead18 = FontBody, *FxInter13 = FontSmall;
const uint8_t *FxBarlow17 = FontSmall, *FxVT22 = FontBody, *FxVT48 = FontTitle;
bool fxThemed = false;

const uint8_t *packFind(const char *name, size_t *len)
{
  if(!ok) return nullptr;
  for(int i = 0; i < count; i++)
    if(!strncmp(table[i].name, name, sizeof(table[i].name))) { if(len) *len = table[i].len; return PACK_DATA + table[i].off; }
  return nullptr;
}

static void font(const char *name, const uint8_t *&slot)
{
  const uint8_t *p = packFind(name, nullptr);
  if(p) slot = p;
}

#ifndef R360_SIM
const esp_partition_t *packPartition()
{
  return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)PACK_SUBTYPE, "assets");
}

// Le o cabecalho direto da flash e mapeia so o tamanho do pacote
static bool mapPack()
{
  const esp_partition_t *p = packPartition();
  if(!p) { why = "sem partição assets"; return false; }
  uint8_t h[20];
  if(esp_partition_read(p, 0, h, sizeof(h)) != ESP_OK) { why = "erro de leitura"; return false; }
  if(memcmp(h, "R360PK01", 8)) { why = "vazio"; return false; }
  uint32_t n; memcpy(&n, h + 12, 4);
  if(n == 0 || n > 256) { why = "cabeçalho inválido"; return false; }
  // tamanho: fim do maior item da tabela
  Entry *t = (Entry *)heap_caps_malloc(n * sizeof(Entry), MALLOC_CAP_SPIRAM);
  if(!t || esp_partition_read(p, 20, t, n * sizeof(Entry)) != ESP_OK) { free(t); why = "erro de leitura"; return false; }
  uint32_t end = 20 + n * sizeof(Entry);
  for(uint32_t i = 0; i < n; i++) if(t[i].off + t[i].len > end) end = t[i].off + t[i].len;
  free(t);
  if(end > p->size) { why = "maior que a partição"; return false; }
  const void *ptr = nullptr;
  const uint32_t mapLen = (end + 0xFFFF) & ~0xFFFFu;
  if(esp_partition_mmap(p, 0, mapLen > p->size ? p->size : mapLen, ESP_PARTITION_MMAP_DATA, &ptr, &mapHandle) != ESP_OK) { why = "sem espaço para mapear"; return false; }
  mapped = true;
  PACK_DATA = (const uint8_t *)ptr;
  PACK_SIZE = end;
  return true;
}
#endif

bool packBegin()
{
#ifndef R360_SIM
  if(!mapped && !mapPack()) return false;
#endif
  if(PACK_SIZE < 20 || memcmp(PACK_DATA, "R360PK01", 8)) { why = "vazio"; return false; }
  memcpy(&version, PACK_DATA + 8, 4);
  uint32_t n; memcpy(&n, PACK_DATA + 12, 4);
  if(n == 0 || n > 256 || 20 + n * sizeof(Entry) > PACK_SIZE) { why = "cabeçalho inválido"; return false; }
  uint32_t crc; memcpy(&crc, PACK_DATA + 16, 4);
  const uint32_t head = 20 + n * sizeof(Entry);
#ifdef R360_SIM
  (void)crc; (void)head;
#else
  // Confere o pacote inteiro (~45 ms): um envio cortado no meio nao chega a ser usado
  if((esp_rom_crc32_le(0, PACK_DATA + head, PACK_SIZE - head)) != crc) { why = "corrompido (CRC)"; return false; }
#endif
  table = (Entry *)malloc(n * sizeof(Entry));
  if(!table) { why = "sem memória"; return false; }
  memcpy(table, PACK_DATA + 20, n * sizeof(Entry));
  count = n;
  for(int i = 0; i < count; i++) if(table[i].off + table[i].len > PACK_SIZE) { count = 0; why = "tabela inválida"; return false; }
  if(version < PACK_NEEDED) { count = 0; why = "versão antiga"; return false; }
  why = "";
  ok = true;
  size_t len = 0;
  sil30 = packFind("sil_30", &len); if(len != 7 * 36 * 900 / 2) sil30 = nullptr;
  sil150 = packFind("sil_150", &len); if(len != 7 * 150 * 150 / 2) sil150 = nullptr;
  font("vClockXL", FxClockXL); font("vClockL", FxClockL); font("vMinimal", FxMinimal); font("vLight30", FxLight30);
  font("vCinzel34", FxCinzel34); font("vCinzel20", FxCinzel20); font("vCinzel14", FxCinzel14);
  font("vSerif30", FxSerif30); font("vSerif22", FxSerif22); font("vMono14", FxMono14); font("vMono20", FxMono20);
  font("vMono30", FxMono30); font("vFell18", FxFell18); font("vFellIt18", FxFellIt18); font("vFell30", FxFell30);
  font("vOsw14", FxOsw14); font("vOsw18", FxOsw18); font("vOsw34", FxOsw34); font("vHead18", FxHead18);
  font("vInter13", FxInter13); font("vBarlow17", FxBarlow17); font("vVT22", FxVT22); font("vVT48", FxVT48);
  fxThemed = FxCinzel34 != FontTitle;
  return true;
}

bool packOk() { return ok; }
const char *packWhy() { return why; }

void packDisable()
{
  // Antes de apagar a particao: tudo volta para as fontes internas e nada mais aponta para o pacote
  ok = false; why = "atualizando";
  sil30 = sil150 = nullptr;
  FxClockXL = FxClockL = FxMinimal = FxMono30 = FontClock; FxLight30 = FxCinzel34 = FxSerif30 = FxFell30 = FxOsw34 = FxVT48 = FontTitle;
  FxCinzel20 = FxSerif22 = FxMono20 = FxOsw18 = FxHead18 = FxVT22 = FontBody;
  FxCinzel14 = FxMono14 = FxFell18 = FxFellIt18 = FxOsw14 = FxInter13 = FxBarlow17 = FontSmall;
  fxThemed = false;
}
uint32_t packVersion() { return ok ? version : 0; }
uint32_t packBytes() { return ok ? PACK_SIZE : 0; }
int packItems() { return ok ? count : 0; }

const uint8_t *packSil30(int cat, int a)
{
  if(!sil30 || cat < 0 || cat > 6) return nullptr;
  a %= 36; if(a < 0) a += 36;
  return sil30 + (cat * 36 + a) * 450;
}

const uint8_t *packSil150(int cat)
{
  if(!sil150 || cat < 0 || cat > 6) return nullptr;
  return sil150 + cat * 11250;
}

const uint8_t *packZodiac(int sign, bool big)
{
  if(sign < 0 || sign > 11) return nullptr;
  size_t len = 0;
  const uint8_t *p = packFind(big ? "zod_96" : "zod_40", &len);
  const int sz = big ? 96 : 40;
  if(!p || len != (size_t)12 * sz * sz / 2) return nullptr;
  return p + sign * sz * sz / 2;
}
