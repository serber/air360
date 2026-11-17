#pragma once

#include <chrono>
#include <mutex>

#include "freertos/FreeRTOS.h"

// Host mutex semaphore backed by std::timed_mutex, so concurrency tests that
// run real threads exercise actual mutual exclusion (and ThreadSanitizer sees
// the synchronization). One tick is one millisecond (see pdMS_TO_TICKS).
struct StaticSemaphore_t {
    std::timed_mutex mutex;
};

using SemaphoreHandle_t = StaticSemaphore_t*;

inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t* buffer) {
    return buffer;
}

inline BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks) {
    if (semaphore == nullptr) {
        return pdFALSE;
    }
    if (ticks == portMAX_DELAY) {
        semaphore->mutex.lock();
        return pdTRUE;
    }
    if (ticks == 0U) {
        return semaphore->mutex.try_lock() ? pdTRUE : pdFALSE;
    }
    return semaphore->mutex.try_lock_for(std::chrono::milliseconds(ticks)) ? pdTRUE : pdFALSE;
}

inline BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
    if (semaphore == nullptr) {
        return pdFALSE;
    }
    semaphore->mutex.unlock();
    return pdTRUE;
}
