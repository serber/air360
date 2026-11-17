#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>

#include "freertos/FreeRTOS.h"

// Host event group: bits guarded by a mutex + condition variable so
// xEventGroupWaitBits() really blocks until another thread sets them.
struct StaticEventGroup_t {
    StaticEventGroup_t() {}
    std::mutex mutex;
    std::condition_variable cv;
    EventBits_t bits = 0U;
};

using EventGroupHandle_t = StaticEventGroup_t*;

inline EventGroupHandle_t xEventGroupCreateStatic(StaticEventGroup_t* buffer) {
    return buffer;
}

inline EventBits_t xEventGroupSetBits(EventGroupHandle_t group, EventBits_t bits) {
    // Notify while holding the mutex: a waiter that wakes may destroy the
    // group's owner immediately (e.g. stop() returning and the manager going
    // out of scope), so the setter must not touch the condition variable
    // after releasing the lock. FreeRTOS sets bits atomically as well.
    std::lock_guard<std::mutex> guard(group->mutex);
    group->bits |= bits;
    group->cv.notify_all();
    return group->bits;
}

inline EventBits_t xEventGroupClearBits(EventGroupHandle_t group, EventBits_t bits) {
    std::lock_guard<std::mutex> guard(group->mutex);
    const EventBits_t previous = group->bits;
    group->bits &= ~bits;
    return previous;
}

inline EventBits_t xEventGroupGetBits(EventGroupHandle_t group) {
    std::lock_guard<std::mutex> guard(group->mutex);
    return group->bits;
}

inline EventBits_t xEventGroupWaitBits(
    EventGroupHandle_t group,
    EventBits_t bits,
    BaseType_t clear_on_exit,
    BaseType_t wait_for_all,
    TickType_t ticks) {
    std::unique_lock<std::mutex> lock(group->mutex);
    const auto satisfied = [group, bits, wait_for_all] {
        return wait_for_all == pdTRUE ? (group->bits & bits) == bits : (group->bits & bits) != 0U;
    };
    if (ticks == portMAX_DELAY) {
        group->cv.wait(lock, satisfied);
    } else {
        group->cv.wait_for(lock, std::chrono::milliseconds(ticks), satisfied);
    }
    const EventBits_t result = group->bits;
    if (satisfied() && clear_on_exit == pdTRUE) {
        group->bits &= ~bits;
    }
    return result;
}
