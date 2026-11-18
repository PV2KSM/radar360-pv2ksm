#pragma once
// Descobre os aparelhos ativos na rede local (Wi-Fi):
//  - varredura ARP: manda um ARP request para cada IP da sub-rede; todo aparelho responde ARP para
//    existir na rede (pega ate celular/IoT que bloqueiam ping). Le a tabela ARP em lotes (so cabe 10).
//  - nome: consulta NBSTAT (NetBIOS node status, UDP 137) em cada IP ativo -> nome do aparelho.
//  - fabricante: pelos 3 primeiros bytes do MAC (OUI), com uma tabela embutida dos mais comuns.
#include <stdint.h>

struct LanDev
{
  uint8_t ip[4];
  uint8_t mac[6];
  char name[20];
  const char *vendor;   // nullptr = desconhecido
  uint32_t seen;
};

void lanBegin();
void lanScanStart();     // inicia uma varredura (ignora se ja estiver rodando)
void lanWork();          // chamar no loop
bool lanScanning();
int  lanProgress();      // 0..100
int  lanCount();         // aparelhos encontrados
int  lanList(LanDev *out, int max);   // copia a lista (ordenada por IP)
uint32_t lanScanAge();   // ms desde a ultima varredura concluida (0 = nunca)
const char *lanVendorOui(const uint8_t mac[6]);   // fabricante pelo OUI (nullptr se nao conhecido)
