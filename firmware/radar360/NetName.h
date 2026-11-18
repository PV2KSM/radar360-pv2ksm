#pragma once
// Nome de computador Windows (ex.: DESKTOP-PC) -> IP, por NetBIOS e LLMNR na rede local
#include <Arduino.h>
#include <WiFi.h>

bool netLocalNameLookup(const char *name, IPAddress &out);
