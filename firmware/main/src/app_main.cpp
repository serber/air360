#include "air360/app.hpp"

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Called by FreeRTOS when the canary at the bottom of any task stack is
// overwritten. Logs the task name and reboots so the watchdog does not
// silently proceed with a corrupted stack.
extern "C" void vApplicationStackOverflowHook(TaskHandle_t task, char* name) {
    static_cast<void>(task);
    ESP_EARLY_LOGE("air360.app", "Stack overflow in task: %s",
                   name != nullptr ? name : "unknown");
    esp_restart();
}

extern "C" void app_main(void) {
    static air360::App app;
    app.run();
}
