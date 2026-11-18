#pragma once
#include <cstdint>
typedef void *SemaphoreHandle_t;
typedef void *TaskHandle_t;
#define portMAX_DELAY 0xFFFFFFFF
#define pdMS_TO_TICKS(x) (x)
#define tskIDLE_PRIORITY 0
static inline SemaphoreHandle_t xSemaphoreCreateMutex() { return (void *)1; }
static inline int xSemaphoreTake(SemaphoreHandle_t, uint32_t) { return 1; }
static inline int xSemaphoreGive(SemaphoreHandle_t) { return 1; }
static inline void vTaskDelay(uint32_t) {}
static inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() { return (void *)1; }
static inline int xSemaphoreTakeRecursive(SemaphoreHandle_t, uint32_t) { return 1; }
static inline int xSemaphoreGiveRecursive(SemaphoreHandle_t) { return 1; }
