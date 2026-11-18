#pragma once
#include <Arduino.h>
struct WiFiStub {
  int hostByName(const char *, IPAddress &ip) { ip = IPAddress(0xC0A80032); return 1; }
  String SSID() { return String("TERNET_EXT"); }
  int status() { return 3; }
  int channel() { return 6; }
};
extern WiFiStub WiFi;
