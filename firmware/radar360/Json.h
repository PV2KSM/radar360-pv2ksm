#pragma once
// Alocador do ArduinoJson na PSRAM (respostas grandes de voos/clima nao ocupam a RAM interna)
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

struct SpiRamAllocator : ArduinoJson::Allocator
{
  void *allocate(size_t size) override { return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
  void deallocate(void *p) override { heap_caps_free(p); }
  void *reallocate(void *p, size_t n) override { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
  static SpiRamAllocator *instance() { static SpiRamAllocator a; return &a; }
};
