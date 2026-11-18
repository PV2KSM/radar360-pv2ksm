// Nome de PC Windows -> IP pela rede local, sem DNS:
//  - NetBIOS (NBNS, UDP 137, broadcast): o jeito classico, o Windows responde por padrao em rede privada
//  - LLMNR (UDP 5355, multicast 224.0.0.252): o substituto do NetBIOS no Windows 10/11
#include "NetName.h"
#ifndef R360_SIM
#include <WiFi.h>
#include <WiFiUdp.h>

// PC com varias placas de rede (VirtualBox, VPN, cabo + Wi-Fi) responde com todos os enderecos.
// Guarda todos e escolhe o que esta na mesma rede do Radar; o endereco de onde veio a resposta tambem conta.
struct Cands { IPAddress ip[10]; int n = 0; void add(const IPAddress &a) { if(a == IPAddress((uint32_t)0) || n >= 10) return; for(int i = 0; i < n; i++) if(ip[i] == a) return; ip[n++] = a; } };

static bool pick(const Cands &c, IPAddress &out)
{
  if(!c.n) return false;
  const uint32_t mask = (uint32_t)WiFi.subnetMask(), me = (uint32_t)WiFi.localIP();
  for(int i = 0; i < c.n; i++) if(((uint32_t)c.ip[i] & mask) == (me & mask)) { out = c.ip[i]; return true; }
  out = c.ip[0];
  return true;
}

static bool waitReply(WiFiUDP &udp, uint8_t *buf, int cap, int &len, uint16_t id, uint32_t ms)
{
  const uint32_t t0 = millis();
  while(millis() - t0 < ms)
  {
    const int n = udp.parsePacket();
    if(n > 0)
    {
      len = udp.read(buf, cap);
      if(len >= 12 && ((buf[0] << 8) | buf[1]) == id && (buf[2] & 0x80)) return true;   // resposta com o nosso id
    }
    delay(15);
  }
  return false;
}

// pula um nome (rotulos ou ponteiro de compressao) a partir de p
static int skipName(const uint8_t *b, int len, int p)
{
  while(p < len)
  {
    const uint8_t l = b[p];
    if(l == 0) return p + 1;
    if((l & 0xC0) == 0xC0) return p + 2;
    p += 1 + l;
  }
  return -1;
}

static bool nbns(const char *name, Cands &c)
{
  WiFiUDP udp;
  if(!udp.begin(0)) return false;
  uint8_t q[50]; const uint16_t id = (uint16_t)esp_random();
  memset(q, 0, sizeof(q));
  q[0] = id >> 8; q[1] = id & 255; q[2] = 0x01; q[3] = 0x10;   // consulta, broadcast
  q[5] = 1;                                                     // 1 pergunta
  q[12] = 0x20;
  char nb[16]; memset(nb, ' ', 15); nb[15] = 0;                 // nome NetBIOS: 15 letras + sufixo 0x00
  for(int i = 0; i < 15 && name[i]; i++) nb[i] = toupper((unsigned char)name[i]);
  for(int i = 0; i < 16; i++) { q[13 + i * 2] = 'A' + ((uint8_t)nb[i] >> 4); q[14 + i * 2] = 'A' + ((uint8_t)nb[i] & 15); }
  q[45] = 0; q[46] = 0x00; q[47] = 0x20; q[48] = 0x00; q[49] = 0x01;   // tipo NB, classe IN
  IPAddress bc = WiFi.broadcastIP();
  bool ok = false;
  for(int attempt = 0; attempt < 2 && !ok; attempt++)
  {
    udp.beginPacket(bc, 137); udp.write(q, 50); udp.endPacket();
    uint8_t r[256]; int len = 0;
    if(waitReply(udp, r, sizeof(r), len, id, 900) && ((r[6] << 8) | r[7]) > 0)
    {
      int p = skipName(r, len, 12);
      if(p > 0 && p + 10 <= len)
      {
        const int rdlen = (r[p + 8] << 8) | r[p + 9];
        const int d = p + 10;
        for(int k = 0; k + 6 <= rdlen && d + k + 6 <= len; k += 6) { c.add(IPAddress(r[d + k + 2], r[d + k + 3], r[d + k + 4], r[d + k + 5])); ok = true; }
        if(ok) c.add(udp.remoteIP());
      }
    }
  }
  udp.stop();
  return ok;
}

static bool llmnr(const char *name, Cands &c)
{
  WiFiUDP udp;
  if(!udp.begin(0)) return false;
  uint8_t q[80]; int n = 0; const uint16_t id = (uint16_t)esp_random();
  q[n++] = id >> 8; q[n++] = id & 255; q[n++] = 0; q[n++] = 0; q[n++] = 0; q[n++] = 1;
  for(int i = 0; i < 6; i++) q[n++] = 0;
  const size_t L = min(strlen(name), (size_t)60);
  q[n++] = (uint8_t)L; memcpy(q + n, name, L); n += L; q[n++] = 0;
  q[n++] = 0; q[n++] = 1; q[n++] = 0; q[n++] = 1;   // A, IN
  bool ok = false;
  for(int attempt = 0; attempt < 2 && !ok; attempt++)
  {
    udp.beginPacket(IPAddress(224, 0, 0, 252), 5355); udp.write(q, n); udp.endPacket();
    uint8_t r[300]; int len = 0;
    if(waitReply(udp, r, sizeof(r), len, id, 900))
    {
      const int an = (r[6] << 8) | r[7];
      int p = skipName(r, len, 12); if(p < 0) break;
      p += 4;   // tipo + classe da pergunta
      for(int k = 0; k < an && p > 0 && p + 10 <= len; k++)
      {
        p = skipName(r, len, p); if(p < 0 || p + 10 > len) break;
        const int type = (r[p] << 8) | r[p + 1], rdlen = (r[p + 8] << 8) | r[p + 9];
        const int d = p + 10;
        if(type == 1 && rdlen == 4 && d + 4 <= len) { c.add(IPAddress(r[d], r[d + 1], r[d + 2], r[d + 3])); ok = true; }
        p = d + rdlen;
      }
      if(ok) c.add(udp.remoteIP());
    }
  }
  udp.stop();
  return ok;
}

bool netLocalNameLookup(const char *name, IPAddress &out)
{
  if(!name[0] || strchr(name, '.') || strlen(name) > 15) return false;   // nomes NetBIOS: sem ponto, ate 15 letras
  Cands c;
  if(!nbns(name, c)) llmnr(name, c);
  return pick(c, out);
}
#else
bool netLocalNameLookup(const char *, IPAddress &) { return false; }
#endif
