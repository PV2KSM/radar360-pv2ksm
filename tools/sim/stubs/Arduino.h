// Simulador no PC: o minimo do Arduino para compilar as telas do Radar 360
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <ctime>
#include <string>
#include <algorithm>
#include <strings.h>
#include "freertos/FreeRTOS.h"
#include "esp_heap_caps.h"

#define PROGMEM
#define PI 3.14159265358979f
#define DEG_TO_RAD 0.017453292519943295f
#define RAD_TO_DEG 57.29577951308232f
template<class A, class B> static inline auto min(A a, B b) -> decltype(a < b ? a : b) { return a < b ? a : b; }
template<class A, class B> static inline auto max(A a, B b) -> decltype(a < b ? a : b) { return a > b ? a : b; }
template<class T, class L, class H> static inline T constrain(T x, L lo, H hi) { return x < lo ? (T)lo : x > hi ? (T)hi : x; }

extern uint32_t simMillis;
static inline uint32_t millis() { return simMillis; }
static inline void delay(uint32_t ms) { simMillis += ms; }
static inline uint32_t esp_random() { return (uint32_t)rand(); }

class String : public std::string
{
public:
  String() {}
  String(const char *s) : std::string(s ? s : "") {}
  String(const std::string &s) : std::string(s) {}
  String(int v) : std::string(std::to_string(v)) {}
  bool endsWith(const char *s) const { size_t n = strlen(s); return size() >= n && compare(size() - n, n, s) == 0; }
  bool startsWith(const char *s) const { return rfind(s, 0) == 0; }
  int length() const { return (int)size(); }
  String operator+(const String &o) const { return String(std::string(*this) + std::string(o)); }
  String operator+(const char *o) const { return String(std::string(*this) + o); }
};
static inline String operator+(const char *a, const String &b) { return String(std::string(a) + std::string(b)); }

struct SerialStub { template<class... A> void printf(const char *f, A... a) { ::printf(f, a...); } void println(const char *s) { puts(s); } void begin(int) {} };
extern SerialStub Serial;

struct IPAddress
{
  uint32_t v = 0;
  IPAddress() {}
  IPAddress(uint32_t x) : v(x) {}
  bool fromString(const char *s) { int a, b, c, d; if(sscanf(s, "%d.%d.%d.%d", &a, &b, &c, &d) == 4) { v = (a << 24) | (b << 16) | (c << 8) | d; return true; } return false; }
  bool operator!=(const IPAddress &o) const { return v != o.v; }
  String toString() const { char b[20]; snprintf(b, sizeof(b), "%u.%u.%u.%u", v >> 24, (v >> 16) & 255, (v >> 8) & 255, v & 255); return String(b); }
};
