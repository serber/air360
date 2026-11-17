#pragma once
#include "esp_err.h"
#include "freertos/task.h"
// Host tasks are not watched; subscription calls always succeed.
inline esp_err_t esp_task_wdt_add(TaskHandle_t) { return ESP_OK; }
inline esp_err_t esp_task_wdt_reset() { return ESP_OK; }
inline esp_err_t esp_task_wdt_delete(TaskHandle_t) { return ESP_OK; }
inline esp_err_t esp_task_wdt_status(TaskHandle_t) { return ESP_OK; }
