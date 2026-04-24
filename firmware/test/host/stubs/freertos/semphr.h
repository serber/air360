#pragma once

#include "freertos/FreeRTOS.h"

struct StaticSemaphore_t {
    int unused = 0;
};

using SemaphoreHandle_t = StaticSemaphore_t*;

inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t* buffer) {
    return buffer;
}

inline BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t) {
    return pdTRUE;
}

inline BaseType_t xSemaphoreGive(SemaphoreHandle_t) {
    return pdTRUE;
}
