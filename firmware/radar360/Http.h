#pragma once
#include <Arduino.h>

// Buffer em PSRAM para respostas HTTP
struct HttpBuf
{
  uint8_t *data = nullptr;
  size_t len = 0;
  size_t cap = 0;
  bool truncated = false;
  bool reserve(size_t n);
  bool grow(size_t need, size_t limit);   // aumenta (PSRAM) para caber "need" bytes, ate "limit"
  void release();
  const char *str() const { return (const char *)data; }   // sempre terminado em zero
};

// GET simples. https:// usa o pacote de certificados do ESP-IDF; http:// e para a rede local.
// truncate=true guarda o que couber (feeds grandes) em vez de falhar.
// Devolve o codigo HTTP (200 = ok) ou negativo em erro de rede.
int httpGet(const char *url, HttpBuf &out, size_t maxLen, char *err = nullptr, size_t errLen = 0,
            const char *accept = nullptr, bool truncate = false, uint32_t timeoutMs = 15000);

const char *httpUserAgent();
bool httpBusy();   // download em andamento (a tela alivia as animacoes para o Wi-Fi respirar)

void tlsUsePsram();   // chamar no inicio: buffers grandes do TLS na PSRAM
