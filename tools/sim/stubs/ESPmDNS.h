#pragma once
#include <Arduino.h>
struct MDNSStub { IPAddress queryHost(const char *, int) { return IPAddress(0xC0A80032); } };
extern MDNSStub MDNS;
