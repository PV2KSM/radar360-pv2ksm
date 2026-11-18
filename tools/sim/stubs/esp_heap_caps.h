#pragma once
#include <cstdlib>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_INTERNAL 4
static inline void *heap_caps_malloc(size_t n, int) { return malloc(n); }
static inline void *heap_caps_realloc(void *p, size_t n, int) { return realloc(p, n); }
static inline void heap_caps_free(void *p) { free(p); }
