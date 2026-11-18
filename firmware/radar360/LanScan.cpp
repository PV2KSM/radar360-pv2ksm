#include "LanScan.h"
#include <string.h>

static const int MAX_LAN = 64;
static LanDev devs[MAX_LAN];
static volatile int devN = 0;
static uint32_t doneAt = 0;

// ---- Tabela de fabricantes por OUI (3 primeiros bytes do MAC) ----
// Lista curta dos mais comuns em rede domestica. Desconhecido -> nullptr.
struct Oui { uint8_t p[3]; const char *name; };
static const Oui OUIS[] = {
  // Espressif (ESP32/ESP8266 - inclui o proprio Radar e muito IoT)
  {{0x24,0x0A,0xC4},"Espressif"},{{0x24,0x6F,0x28},"Espressif"},{{0x30,0xAE,0xA4},"Espressif"},
  {{0x3C,0x61,0x05},"Espressif"},{{0x7C,0xDF,0xA1},"Espressif"},{{0x84,0xF3,0xEB},"Espressif"},
  {{0x8C,0xAA,0xB5},"Espressif"},{{0xA0,0x20,0xA6},"Espressif"},{{0xA4,0xCF,0x12},"Espressif"},
  {{0xB4,0xE6,0x2D},"Espressif"},{{0xBC,0xDD,0xC2},"Espressif"},{{0xC4,0x4F,0x33},"Espressif"},
  {{0xCC,0x50,0xE3},"Espressif"},{{0xDC,0x4F,0x22},"Espressif"},{{0xEC,0xFA,0xBC},"Espressif"},
  // Apple
  {{0x00,0x03,0x93},"Apple"},{{0x3C,0x15,0xC2},"Apple"},{{0x40,0xCB,0xC0},"Apple"},
  {{0x58,0x55,0xCA},"Apple"},{{0x6C,0x40,0x08},"Apple"},{{0x7C,0xD1,0xC3},"Apple"},
  {{0x88,0x66,0xA5},"Apple"},{{0xA4,0x83,0xE7},"Apple"},{{0xAC,0xBC,0x32},"Apple"},
  {{0xDC,0x2B,0x2A},"Apple"},{{0xF0,0x18,0x98},"Apple"},{{0xF4,0xF1,0x5A},"Apple"},
  // Samsung
  {{0x00,0x12,0xFB},"Samsung"},{{0x08,0x37,0x3D},"Samsung"},{{0x30,0xCD,0xA7},"Samsung"},
  {{0x34,0x23,0xBA},"Samsung"},{{0x5C,0x0A,0x5B},"Samsung"},{{0x78,0x1F,0xDB},"Samsung"},
  {{0x8C,0x77,0x12},"Samsung"},{{0xBC,0x14,0x85},"Samsung"},{{0xE8,0x50,0x8B},"Samsung"},
  // Xiaomi
  {{0x28,0x6C,0x07},"Xiaomi"},{{0x34,0xCE,0x00},"Xiaomi"},{{0x50,0x8F,0x4C},"Xiaomi"},
  {{0x64,0x09,0x80},"Xiaomi"},{{0x78,0x11,0xDC},"Xiaomi"},{{0xF8,0xA4,0x5F},"Xiaomi"},
  // Raspberry Pi
  {{0xB8,0x27,0xEB},"Raspberry Pi"},{{0xDC,0xA6,0x32},"Raspberry Pi"},{{0xE4,0x5F,0x01},"Raspberry Pi"},
  {{0x28,0xCD,0xC1},"Raspberry Pi"},
  // TP-Link
  {{0x50,0xC7,0xBF},"TP-Link"},{{0x54,0xAF,0x97},"TP-Link"},{{0x98,0xDA,0xC4},"TP-Link"},
  {{0xAC,0x84,0xC6},"TP-Link"},{{0xC0,0x06,0xC3},"TP-Link"},
  // Intel
  {{0x34,0x13,0xE8},"Intel"},{{0x3C,0xA9,0xF4},"Intel"},{{0x7C,0xB0,0xC2},"Intel"},
  {{0x94,0x65,0x9C},"Intel"},{{0xA0,0xA8,0xCD},"Intel"},{{0xE4,0xA7,0xA0},"Intel"},
  // Amazon
  {{0x44,0x65,0x0D},"Amazon"},{{0x68,0x37,0xE9},"Amazon"},{{0xF0,0x81,0x73},"Amazon"},{{0xFC,0x65,0xDE},"Amazon"},
  // Google / Nest
  {{0x1C,0x53,0xF9},"Google"},{{0x3C,0x28,0x6D},"Google"},{{0xD8,0x6C,0x63},"Google"},{{0xF4,0xF5,0xD8},"Google"},
  // outros comuns
  {{0x00,0x1A,0x11},"Google"},{{0x18,0xB4,0x30},"Nest"},{{0xB0,0x4E,0x26},"TP-Link"},
  {{0x00,0x17,0x88},"Philips Hue"},{{0xEC,0xB5,0xFA},"Philips Hue"},
  {{0x10,0x27,0xBE},"Realtek"},{{0x52,0x54,0x00},"QEMU/VM"},{{0x00,0x50,0x56},"VMware"},
  {{0x08,0x00,0x27},"VirtualBox"},{{0xD8,0x0D,0x17},"TP-Link"},
  {{0x2C,0xF0,0x5D},"Sony"},{{0x54,0x27,0x1E},"LG"},{{0x00,0x1D,0xBA},"Sony"},
  {{0x10,0x2B,0x41},"Tuya"},{{0x68,0x57,0x2D},"Tuya"},{{0xD4,0xA6,0x51},"Tuya"},
  {{0x00,0x0E,0xC6},"Asix"},{{0x40,0xED,0x00},"Motorola"},{{0xE0,0xCC,0x7A},"Motorola"},
};

const char *lanVendorOui(const uint8_t mac[6])
{
  for(const Oui &o : OUIS)
    if(o.p[0] == mac[0] && o.p[1] == mac[1] && o.p[2] == mac[2]) return o.name;
  return nullptr;
}

#ifndef R360_SIM
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "lwip/netif.h"
#include "lwip/etharp.h"
#include "lwip/tcpip.h"
#include "lwip/ip4_addr.h"

// ---- estado da varredura ----
enum Phase { IDLE, SWEEP, NAMES, FINISH };
static volatile Phase phase = IDLE;
static uint8_t base[4];          // rede (ultimo octeto = 0)
static volatile int host = 0;    // octeto atual sendo varrido (1..254)
static volatile int nameIdx = 0; // indice do proximo aparelho a resolver nome
static uint8_t selfOct = 0;    // nosso ultimo octeto (pra excluir o proprio Radar)

static bool findDev(const uint8_t ip[4], int &out)
{
  for(int i = 0; i < devN; i++) if(!memcmp(devs[i].ip, ip, 4)) { out = i; return true; }
  return false;
}

// Interface do Wi-Fi (STA): a que tem o nosso IP. Fallback: netif_default.
static struct netif *staNetif()
{
  const uint32_t me = (uint32_t)WiFi.localIP();
  struct netif *nif;
  NETIF_FOREACH(nif)
    if(nif && netif_is_up(nif) && ip4_addr_get_u32(netif_ip4_addr(nif)) == me) return nif;
  return netif_default;
}

// Manda ARP request para um IP (seguro: core locking do lwIP ligado neste build)
static void arpRequest(const uint8_t ip[4])
{
  struct netif *nif = staNetif();
  if(!nif) return;
  ip4_addr_t a; IP4_ADDR(&a, ip[0], ip[1], ip[2], ip[3]);
  LOCK_TCPIP_CORE();
  etharp_request(nif, &a);
  UNLOCK_TCPIP_CORE();
}

// Le a tabela ARP inteira e guarda quem esta na nossa sub-rede. Chamado a cada ciclo durante a varredura:
// pega cada aparelho assim que ele responde, antes de a entrada (so cabem 10) ser descartada.
static void collectArp()
{
  LOCK_TCPIP_CORE();
  for(size_t i = 0; i < ARP_TABLE_SIZE; i++)
  {
    ip4_addr_t *ip = nullptr; struct netif *nif = nullptr; struct eth_addr *eth = nullptr;
    if(etharp_get_entry(i, &ip, &nif, &eth) && ip && eth)
    {
      const uint32_t v = ip4_addr_get_u32(ip);
      const uint8_t o[4] = { (uint8_t)(v & 0xFF), (uint8_t)((v >> 8) & 0xFF), (uint8_t)((v >> 16) & 0xFF), (uint8_t)((v >> 24) & 0xFF) };
      if(o[0] == base[0] && o[1] == base[1] && o[2] == base[2] && o[3] != 0 && o[3] != selfOct)
      {
        int at;
        if(!findDev(o, at))
        {
          if(devN >= MAX_LAN) continue;
          at = devN++; memset(&devs[at], 0, sizeof(LanDev)); memcpy(devs[at].ip, o, 4);
        }
        memcpy(devs[at].mac, eth->addr, 6);
        devs[at].vendor = lanVendorOui(eth->addr);
        devs[at].seen = millis();
      }
    }
  }
  UNLOCK_TCPIP_CORE();
}

// NBSTAT (NetBIOS node status) em <ip>:137 -> nome do aparelho. Best-effort.
static bool nbstat(const uint8_t ip[4], char *out, int cap)
{
  WiFiUDP udp;
  if(!udp.begin(0)) return false;
  uint8_t req[50];
  memset(req, 0, sizeof(req));
  req[0] = 0x13; req[1] = 0x37;        // transaction id qualquer
  req[2] = 0x00; req[3] = 0x00;        // flags
  req[4] = 0x00; req[5] = 0x01;        // qdcount = 1
  // nome codificado "*" (0x2A) + zeros, half-ascii
  int p = 12;
  req[p++] = 0x20;                     // tamanho do nome codificado = 32
  const uint8_t nm[16] = {0x2A};       // "*" e o resto 0
  for(int i = 0; i < 16; i++) { req[p++] = 'A' + (nm[i] >> 4); req[p++] = 'A' + (nm[i] & 0x0F); }
  req[p++] = 0x00;                     // fim do nome
  req[p++] = 0x00; req[p++] = 0x21;    // qtype = NBSTAT (0x0021)
  req[p++] = 0x00; req[p++] = 0x01;    // qclass = IN
  IPAddress dst(ip[0], ip[1], ip[2], ip[3]);
  udp.beginPacket(dst, 137); udp.write(req, p); udp.endPacket();

  uint8_t buf[512];
  const uint32_t t0 = millis();
  int len = 0;
  while(millis() - t0 < 350)
  {
    const int n = udp.parsePacket();
    if(n > 0) { len = udp.read(buf, sizeof(buf)); break; }
    delay(10);
  }
  udp.stop();
  if(len < 57) return false;
  // pula cabecalho(12) + question(34 nome + 4 tipo/classe) -> answer name(34)+type(2)+class(2)+ttl(4)+rdlen(2)
  int q = 12 + 34 + 4;        // inicio do answer
  q += 34 + 2 + 2 + 4 + 2;    // nome + type + class + ttl + rdlength
  if(q >= len) return false;
  const int num = buf[q++];
  for(int i = 0; i < num && q + 18 <= len; i++)
  {
    char nmv[16]; memcpy(nmv, buf + q, 15); nmv[15] = 0;
    const uint8_t type = buf[q + 15];
    const uint16_t flags = (buf[q + 16] << 8) | buf[q + 17];
    q += 18;
    const bool group = flags & 0x8000;
    if(type == 0x00 && !group)         // nome unico de estacao de trabalho
    {
      int e = 14; while(e >= 0 && (nmv[e] == ' ' || nmv[e] == 0)) e--;
      nmv[e + 1] = 0;
      if(nmv[0]) { strlcpy(out, nmv, cap); return true; }
    }
  }
  return false;
}

void lanBegin() {}

static TaskHandle_t lanTaskH = nullptr;

// A varredura roda em TAREFA PROPRIA: le o ARP a cada ~12 ms, independente dos downloads (voo/clima/
// noticias) que travam segundos no workerTask. Sem isso, a tabela ARP (10 vagas) esvazia antes da leitura
// e a busca pela web acha 0 (so funcionava na tela do aparelho, onde os downloads nao rodam).
static void lanTask(void *)
{
  uint32_t grace = 0;
  while(true)                      // SWEEP: manda ARP e le a tabela inteira, bem rapido
  {
    if(host <= 254)
      for(int k = 0; k < 4 && host <= 254; k++, host++)
      {
        uint8_t ip[4] = { base[0], base[1], base[2], (uint8_t)host };
        arpRequest(ip);
      }
    collectArp();
    if(host > 254)
    {
      if(!grace) { grace = millis(); if(!grace) grace = 1; }
      if(millis() - grace >= 1500) break;      // tempo de gracia p/ ultimas respostas
    }
    vTaskDelay(pdMS_TO_TICKS(12));
  }
  phase = NAMES;                   // nomes (nbstat bloqueia ~350 ms, mas aqui e tarefa propria)
  for(int i = 0; i < devN; i++)
  {
    nameIdx = i;
    char nm[20] = {0};
    if(nbstat(devs[i].ip, nm, sizeof(nm))) strlcpy(devs[i].name, nm, sizeof(devs[i].name));
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  nameIdx = devN;
  doneAt = millis();
  phase = IDLE;
  lanTaskH = nullptr;
  vTaskDelete(nullptr);
}

void lanScanStart()
{
  if(lanTaskH || phase != IDLE) return;             // ja rodando
  if(WiFi.status() != WL_CONNECTED) return;
  IPAddress me = WiFi.localIP();
  base[0] = me[0]; base[1] = me[1]; base[2] = me[2]; base[3] = 0;
  selfOct = me[3];
  devN = 0; host = 1; nameIdx = 0; phase = SWEEP;
  if(xTaskCreatePinnedToCore(lanTask, "lan", 8192, nullptr, 1, &lanTaskH, 0) != pdPASS) { phase = IDLE; lanTaskH = nullptr; }
}

void lanWork() {}      // a varredura roda em tarefa propria (ver lanTask)

bool lanScanning() { return phase != IDLE; }

int lanProgress()
{
  if(phase == IDLE) return doneAt ? 100 : 0;
  if(phase == SWEEP) return host * 70 / 254;                  // 0..70% na varredura
  if(phase == NAMES) return 70 + (devN ? nameIdx * 30 / devN : 30);   // 70..100% nos nomes
  return 100;
}

int lanCount() { return devN; }

int lanList(LanDev *out, int max)
{
  // ordena por ultimo octeto do IP
  static int idx[MAX_LAN];
  for(int i = 0; i < devN; i++) idx[i] = i;
  for(int a = 0; a < devN; a++)
    for(int b = a + 1; b < devN; b++)
      if(devs[idx[b]].ip[3] < devs[idx[a]].ip[3]) { int t = idx[a]; idx[a] = idx[b]; idx[b] = t; }
  int n = 0;
  for(int i = 0; i < devN && n < max; i++) out[n++] = devs[idx[i]];
  return n;
}

uint32_t lanScanAge() { return doneAt ? millis() - doneAt : 0; }

#else   // ---- simulador ----
#include <cstring>
#include <cstdint>
static long simMillisLan();
void lanBegin() {}
static bool simScanned = false;
void lanScanStart()
{
  static const struct { uint8_t h; const char *nm; uint8_t m0, m1, m2; } F[] = {
    {1, "roteador", 0xAC, 0x84, 0xC6}, {12, "DESKTOP-PC", 0x3C, 0xA9, 0xF4}, {23, "", 0xA4, 0x83, 0xE7},
    {45, "Galaxy-S23", 0x34, 0x23, 0xBA}, {77, "radar360", 0x24, 0x6F, 0x28}, {102, "", 0xB8, 0x27, 0xEB},
    {134, "Echo-Dot", 0x44, 0x65, 0x0D}, {201, "", 0x50, 0x8F, 0x4C} };
  devN = 0;
  for(auto &f : F) { LanDev &d = devs[devN]; memset(&d, 0, sizeof(d)); d.ip[0]=192; d.ip[1]=168; d.ip[2]=0; d.ip[3]=f.h; d.mac[0]=f.m0; d.mac[1]=f.m1; d.mac[2]=f.m2; d.mac[5]=f.h; strncpy(d.name, f.nm, sizeof(d.name)-1); d.vendor=lanVendorOui(d.mac); devN++; }
  simScanned = true; doneAt = 1;
}
void lanWork() {}
bool lanScanning() { return false; }
int lanProgress() { return simScanned ? 100 : 0; }
int lanCount() { return devN; }
int lanList(LanDev *out, int max) { int n = devN < max ? devN : max; for(int i = 0; i < n; i++) out[i] = devs[i]; return n; }
uint32_t lanScanAge() { return simScanned ? 1 : 0; }
#endif
