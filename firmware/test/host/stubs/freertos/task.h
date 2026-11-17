#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "freertos/FreeRTOS.h"

// Host task emulation: each xTaskCreate() runs the task function on its own
// std::thread. Direct-to-task notifications are a counter guarded by a
// condition variable, so xTaskNotifyGive()/ulTaskNotifyTake() wake real
// threads. One tick is one millisecond. Task records are kept for the life of
// the process so a handle stays valid even after the task function returned.
struct HostTask {
    std::mutex mutex;
    std::condition_variable cv;
    std::uint32_t notifications = 0U;
};

using TaskHandle_t = HostTask*;
using TaskFunction_t = void (*)(void*);

namespace freertos_test {
inline std::mutex registry_mutex;
inline std::vector<std::unique_ptr<HostTask>> registry;
inline std::atomic<int> running_tasks{0};
inline thread_local HostTask* current_task = nullptr;
inline std::atomic<bool> fail_task_create{false};

inline HostTask* currentTask() {
    if (current_task == nullptr) {
        // Calls from the test's main thread get a lazily created record.
        std::lock_guard<std::mutex> guard(registry_mutex);
        registry.push_back(std::make_unique<HostTask>());
        current_task = registry.back().get();
    }
    return current_task;
}
}  // namespace freertos_test

inline BaseType_t xTaskCreate(
    TaskFunction_t function,
    const char*,
    std::uint32_t,
    void* argument,
    UBaseType_t,
    TaskHandle_t* out_handle) {
    if (freertos_test::fail_task_create.load()) {
        return pdFALSE;
    }
    HostTask* task = nullptr;
    {
        std::lock_guard<std::mutex> guard(freertos_test::registry_mutex);
        freertos_test::registry.push_back(std::make_unique<HostTask>());
        task = freertos_test::registry.back().get();
    }
    if (out_handle != nullptr) {
        *out_handle = task;
    }
    ++freertos_test::running_tasks;
    std::thread([function, argument, task] {
        freertos_test::current_task = task;
        function(argument);
        --freertos_test::running_tasks;
    }).detach();
    return pdPASS;
}

// The firmware calls vTaskDelete(nullptr) as the last statement of a task;
// returning lets the host thread finish.
inline void vTaskDelete(TaskHandle_t) {}

inline void vTaskDelay(TickType_t ticks) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ticks));
}

inline void xTaskNotifyGive(TaskHandle_t task) {
    if (task == nullptr) {
        return;
    }
    {
        std::lock_guard<std::mutex> guard(task->mutex);
        ++task->notifications;
    }
    task->cv.notify_all();
}

inline std::uint32_t ulTaskNotifyTake(BaseType_t clear_on_exit, TickType_t ticks) {
    HostTask* task = freertos_test::currentTask();
    std::unique_lock<std::mutex> lock(task->mutex);
    const auto ready = [task] { return task->notifications > 0U; };
    if (ticks == portMAX_DELAY) {
        task->cv.wait(lock, ready);
    } else {
        task->cv.wait_for(lock, std::chrono::milliseconds(ticks), ready);
    }
    const std::uint32_t value = task->notifications;
    if (value > 0U) {
        task->notifications = clear_on_exit == pdTRUE ? 0U : value - 1U;
    }
    return value;
}

inline UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t) {
    return 0U;
}
