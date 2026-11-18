// Generated from OurAirports (public domain): all Brazilian airports with an ICAO code plus
// every large/medium airport worldwide. Sorted by ICAO for binary search.
#pragma once
#include <stdint.h>
struct AirportRec { char icao[5]; float lat; float lon; char name[24]; };
extern const AirportRec airportTable[];
extern const int airportCount;


#include <string.h>
// Busca binaria pelo codigo ICAO (tabela ordenada)
static inline const AirportRec *airportFind(const char *icao)
{
  int lo = 0, hi = airportCount - 1;
  while(lo <= hi)
  {
    const int mid = (lo + hi) / 2;
    const int c = strncmp(icao, airportTable[mid].icao, 4);
    if(!c) return &airportTable[mid];
    if(c < 0) hi = mid - 1; else lo = mid + 1;
  }
  return nullptr;
}
