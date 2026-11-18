#pragma once
// Pacote visual: fundos realistas, fontes dos temas, silhuetas, bandeiras, lua.
// Gerado por tools/build_pack.py (pack/radar360-pack.bin). Fica na particao "assets" da flash, separada do
// firmware (atualiza pelo painel), e e lido direto dela (sem copiar para a RAM).
#include <Arduino.h>

#define PACK_NEEDED  2       // versao minima do pacote que este firmware entende
#define PACK_SUBTYPE 0x40    // subtipo da particao "assets" (partitions.csv)

bool packBegin();
bool packOk();
const char *packWhy();          // motivo de nao ter pacote ("" se ok)
void packDisable();             // antes de gravar um pacote novo
#ifndef R360_SIM
#include <esp_partition.h>
const esp_partition_t *packPartition();
#endif
uint32_t packVersion();
uint32_t packBytes();
int  packItems();

const uint8_t *packFind(const char *name, size_t *len);   // ponteiro na flash (ou nullptr)
// Mascaras alfa de 4 bits (2 pixels por byte, o primeiro no nibble alto)
const uint8_t *packSil30(int cat, int angleIdx);           // 7 categorias x 36 angulos, 30x30
const uint8_t *packSil150(int cat);                        // 150x150, nariz para cima
const uint8_t *packZodiac(int sign, bool big);             // 40x40 ou 96x96
