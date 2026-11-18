#include "Http.h"
#include <mbedtls/platform.h>
#include <esp_heap_caps.h>

// mbedTLS: blocos grandes (buffers de entrada/saida de ~16 KB, certificados) na PSRAM;
// os pequenos continuam na memoria interna, que e mais rapida.
static void *tlsCalloc(size_t n, size_t size)
{
  const size_t total = n * size;
  if(total >= 1024)
  {
    void *p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if(p) return p;
  }
  void *p = heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  return p ? p : heap_caps_calloc(n, size, MALLOC_CAP_8BIT);
}
static void tlsFree(void *p) { heap_caps_free(p); }
void tlsUsePsram() { mbedtls_platform_set_calloc_free(tlsCalloc, tlsFree); }
#include "Version.h"
#include "Crash.h"
#include <WiFi.h>
#include <NetworkClientSecure.h>
#include <HTTPClient.h>
#include <esp_heap_caps.h>

extern const uint8_t caBundleStart[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t caBundleEnd[] asm("_binary_x509_crt_bundle_end");

static SemaphoreHandle_t tlsLock = nullptr;   // uma conexao TLS por vez (memoria interna)

bool HttpBuf::reserve(size_t n)
{
  if(cap >= n + 1) { len = 0; truncated = false; data[0] = 0; return true; }
  release();
  data = (uint8_t *)heap_caps_malloc(n + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!data) return false;
  cap = n + 1; len = 0; truncated = false; data[0] = 0;
  return true;
}

bool HttpBuf::grow(size_t need, size_t limit)
{
  if(need + 1 <= cap) return true;
  if(need > limit) return false;
  size_t n = max(need + 1, cap * 2);
  if(n > limit + 1) n = limit + 1;
  uint8_t *p = (uint8_t *)heap_caps_realloc(data, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!p) return false;
  data = p; cap = n; data[len] = 0;
  return true;
}

void HttpBuf::release()
{
  if(data) heap_caps_free(data);
  data = nullptr; cap = len = 0;
}

const char *httpUserAgent()
{
  return "Radar360-PV2KSM/" FW_VERSION " (ESP32-S3)";
}

static void setErr(char *err, size_t n, const char *fmt, int v)
{
  if(err && n) snprintf(err, n, fmt, v);
}

static volatile int active = 0;
static volatile uint32_t lastEnd = 0;
bool httpBusy() { return active > 0 || millis() - lastEnd < 400; }

static int httpGetImpl(const char *url, HttpBuf &out, size_t maxLen, char *err, size_t errLen, const char *accept, bool truncate, uint32_t timeoutMs);
int httpGet(const char *url, HttpBuf &out, size_t maxLen, char *err, size_t errLen, const char *accept, bool truncate, uint32_t timeoutMs)
{
  active = active + 1;
  const int r = httpGetImpl(url, out, maxLen, err, errLen, accept, truncate, timeoutMs);
  active = active - 1;
  lastEnd = millis();
  return r;
}

static int httpGetImpl(const char *url, HttpBuf &out, size_t maxLen, char *err, size_t errLen, const char *accept, bool truncate, uint32_t timeoutMs)
{
  if(err && errLen) err[0] = 0;
  if(WiFi.status() != WL_CONNECTED) { setErr(err, errLen, "sem Wi-Fi", 0); return -100; }
  // Comeca pequeno e cresce conforme chega (um bloco enorme reservado de uma vez pode nao existir com a PSRAM
  // fragmentada: era o "sem memoria" da tela PC, que reservava 900 KB para um JSON de ~100 KB)
  if(!out.reserve(min(maxLen, (size_t)32 * 1024))) { setErr(err, errLen, "sem memória", 0); return -101; }
  const bool tls = !strncmp(url, "https://", 8);
  if(!tlsLock) tlsLock = xSemaphoreCreateMutex();
  if(tls && !xSemaphoreTake(tlsLock, pdMS_TO_TICKS(30000))) { setErr(err, errLen, "ocupado", 0); return -102; }
  // A memoria da conexao anterior pode ainda estar sendo devolvida: espera um pouco antes de desistir.
  // (os buffers grandes do TLS vao para a PSRAM - tlsUsePsram - entao a folga interna pedida e pequena)
  if(tls)
  {
    const uint32_t t0 = millis();
    while(!crashInternalHeapOk(16 * 1024) && millis() - t0 < 6000) vTaskDelay(pdMS_TO_TICKS(150));
    if(!crashInternalHeapOk(16 * 1024)) { xSemaphoreGive(tlsLock); setErr(err, errLen, "pouca memória interna", 0); return -106; }
  }

  int result;
  {
    NetworkClientSecure secure;
    NetworkClient plain;
    if(tls) { secure.setCACertBundle(caBundleStart, caBundleEnd - caBundleStart); secure.setHandshakeTimeout(12); }
    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(timeoutMs);
    http.setUserAgent(httpUserAgent());
    http.setReuse(false);
    http.useHTTP10(true);   // sem "chunked": lemos o corpo direto, com pausas para o sistema
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    const bool begun = tls ? http.begin(secure, url) : http.begin(plain, url);
    if(!begun) { setErr(err, errLen, "endereço inválido", 0); result = -103; }
    else
    {
      http.addHeader("Accept", accept ? accept : "*/*");
      const int code = http.GET();
      if(code != HTTP_CODE_OK)
      {
        if(code > 0) setErr(err, errLen, "HTTP %d", code);
        else setErr(err, errLen, "falha de rede (%d)", code);
        result = code;
      }
      else
      {
        // Leitura propria: cede o processador a cada bloco (evita o watchdog em respostas grandes)
        NetworkClient *s = http.getStreamPtr();
        const int total = http.getSize();
        uint8_t chunk[2048];
        uint32_t last = millis(), sinceYield = 0;
        bool overflow = false, timedOut = false, noMem = false;
        if(total > 0 && (size_t)total <= maxLen && !out.grow(total, maxLen)) noMem = true;   // tamanho conhecido: de uma vez
        while(s && !noMem)
        {
          const int avail = s->available();
          if(avail <= 0)
          {
            if(!s->connected()) break;
            if(millis() - last > timeoutMs) { timedOut = true; break; }
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
          }
          const int r = s->read(chunk, min(avail, (int)sizeof(chunk)));
          if(r <= 0) { vTaskDelay(1); continue; }
          last = millis();
          if(out.len + r > maxLen)
          {
            const size_t k = maxLen - out.len;
            if(!out.grow(maxLen, maxLen)) { noMem = true; break; }
            memcpy(out.data + out.len, chunk, k); out.len += k; out.data[out.len] = 0;
            if(truncate) out.truncated = true; else overflow = true;
            break;
          }
          if(!out.grow(out.len + r, maxLen)) { noMem = true; break; }
          memcpy(out.data + out.len, chunk, r); out.len += r; out.data[out.len] = 0;
          if(total > 0 && (int)out.len >= total) break;
          sinceYield += r;
          if(sinceYield >= 8192) { sinceYield = 0; vTaskDelay(1); }
        }
        if(noMem) { setErr(err, errLen, "sem memória", 0); result = -101; }
        else if(overflow) { setErr(err, errLen, "resposta grande demais", 0); result = -104; }
        else if(timedOut && !out.len) { setErr(err, errLen, "tempo esgotado", 0); result = -107; }
        else if(!out.len) { setErr(err, errLen, "resposta vazia", 0); result = -105; }
        else result = 200;
      }
      http.end();
    }
  }
  if(tls) xSemaphoreGive(tlsLock);
  return result;
}
