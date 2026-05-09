#include "air360/app.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstdint>

#include "air360/log_buffer.hpp"
#include "esp_err.h"
#include "led_strip.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.app";
// The maintenance loop only retries SNTP and housekeeping, so 10 s is frequent
// enough for recovery while keeping the main task mostly asleep.
constexpr std::uint32_t kRuntimeMaintenanceDelayMs = 10000U;
// Must be well under CONFIG_ESP_TASK_WDT_TIMEOUT_S so the main task feeds TWDT
// on every slice even if the scheduler is under load.
constexpr std::uint32_t kRuntimeMaintenanceSliceMs = 3000U;
// GPIO48: built-in WS2812 RGB LED on ESP32-S3-DevKitC-1.
constexpr int kRgbLedGpio = 48;
// Keep the status LED intentionally dim so the board remains readable on a
// desk without pulling unnecessary current from the USB rail.
constexpr std::uint8_t kLedBrightness = 16U;  // dim — WS2812 full range is 255

led_strip_handle_t g_led_strip = nullptr;

void setLedColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    if (g_led_strip == nullptr) {
        return;
    }
    if (r == 0U && g == 0U && b == 0U) {
        led_strip_clear(g_led_strip);
    } else {
        led_strip_set_pixel(g_led_strip, 0, r, g, b);
        led_strip_refresh(g_led_strip);
    }
}

esp_err_t initRgbLed() {
    led_strip_config_t strip_config{};
    strip_config.strip_gpio_num = kRgbLedGpio;
    strip_config.max_leds = 1;
    strip_config.led_model = LED_MODEL_WS2812;

    led_strip_rmt_config_t rmt_config{};
    rmt_config.resolution_hz = 10U * 1000U * 1000U;  // 10 MHz

    const esp_err_t err =
        led_strip_new_rmt_device(&strip_config, &rmt_config, &g_led_strip);
    if (err != ESP_OK) {
        return err;
    }

    // Blue while booting.
    led_strip_set_pixel(g_led_strip, 0, 0U, 0U, kLedBrightness);
    led_strip_refresh(g_led_strip);
    return ESP_OK;
}

esp_err_t initWatchdog() {
    // ESP-IDF may pre-initialize TWDT from sdkconfig before app_main().
    esp_err_t err = esp_task_wdt_add(nullptr);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        return ESP_OK;
    }

    esp_task_wdt_config_t config{};
    config.timeout_ms = 30000;
    config.idle_core_mask = (1U << portNUM_PROCESSORS) - 1U;
    config.trigger_panic = true;

    err = esp_task_wdt_init(&config);
    if (err == ESP_OK) {
        err = esp_task_wdt_add(nullptr);
        if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
            return ESP_OK;
        }
    }

    return err;
}

esp_err_t initStorage() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(kTag, "NVS partition required erase before init");
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_flash_erase());
        err = nvs_flash_init();
    }

    return err;
}

esp_err_t initNetworkingCore() {
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    return ESP_OK;
}

// Returns true and lights the red LED if `err` is fatal; returns false on
// success so the caller can `if (reportBootError(...)) return;`.
bool reportBootError(const char* what, esp_err_t err) {
    if (err == ESP_OK) {
        return false;
    }
    ESP_LOGE(kTag, "%s failed: %s", what, esp_err_to_name(err));
    setLedColor(kLedBrightness, 0U, 0U);
    return true;
}

}  // namespace

App::App() : status_service_(platform_.buildInfo()) {}

void App::run() {
    bootInstrumentation();

    if (!bootSystem()) {
        runFailedBootLoop();
        return;
    }

    platform_.boot(status_service_);
    network_.bootCellular(platform_, status_service_);
    data_.bootSensors(platform_, status_service_);
    data_.bootBackends(status_service_);
    network_.bootWifi(platform_, status_service_);
    data_.bootUploads(platform_, network_, status_service_);

    if (!bootWebServer()) {
        runFailedBootLoop();
        return;
    }
    indicateReady();

    runMaintenanceLoop();
}

void App::bootInstrumentation() {
    logBufferInstall();

    const esp_err_t leds_err = initRgbLed();
    if (leds_err != ESP_OK) {
        ESP_LOGW(kTag, "RGB LED setup failed: %s", esp_err_to_name(leds_err));
    }
}

bool App::bootSystem() {
    ESP_LOGI(kTag, "Boot step 1/9: arm task watchdog");
    const esp_err_t watchdog_err = initWatchdog();
    if (watchdog_err != ESP_OK) {
        ESP_LOGW(kTag, "Watchdog setup failed: %s", esp_err_to_name(watchdog_err));
    } else {
        ESP_LOGI(kTag, "TWDT: app_main subscribed (30 s, panic enabled)");
    }
    status_service_.markWatchdogArmed(watchdog_err == ESP_OK);

    ESP_LOGI(kTag, "Boot step 2/9: initialize NVS");
    if (reportBootError("NVS init", initStorage())) {
        return false;
    }
    status_service_.markNvsReady(true);

    ESP_LOGI(kTag, "Boot step 3/9: initialize network core");
    if (reportBootError("Network core init", initNetworkingCore())) {
        return false;
    }

    return true;
}

bool App::bootWebServer() {
    ESP_LOGI(kTag, "Boot step 9/9: start status web server");
    const esp_err_t web_err =
        web_server_.start(
            status_service_,
            network_.networkManager(),
            platform_.deviceConfigRepo(),
            platform_.deviceConfig(),
            data_.sensorConfigRepo(),
            data_.sensorConfigList(),
            data_.sensorManager(),
            data_.measurementStore(),
            data_.backendConfigRepo(),
            platform_.air360Credentials(),
            data_.backendConfigList(),
            data_.uploadManager(),
            network_.cellularConfigRepo(),
            network_.cellularConfig(),
            platform_.deviceConfig().http_port);
    if (reportBootError("Web server start", web_err)) {
        return false;
    }
    status_service_.setWebServerStarted(true);
    return true;
}

void App::indicateReady() {
    ESP_LOGI(
        kTag,
        "Runtime ready on port %" PRIu16,
        platform_.deviceConfig().http_port);
    if (network_.networkManager().state().mode == NetworkMode::kSetupAp) {
        setLedColor(kLedBrightness, 0U, kLedBrightness / 2U);  // pink — AP mode
    } else {
        setLedColor(0U, kLedBrightness, 0U);  // green — station mode
    }
}

void App::runMaintenanceLoop() {
    for (;;) {
        const NetworkState network_state = network_.networkManager().state();
        if (network_state.mode == NetworkMode::kStation &&
            network_state.station_connected &&
            !network_.networkManager().hasValidTime()) {
            const esp_err_t time_err = network_.networkManager().ensureStationTime(10000U);
            if (time_err != ESP_OK) {
                ESP_LOGW(
                    kTag,
                    "Background time sync retry failed: %s",
                    esp_err_to_name(time_err));
            }
        }

        status_service_.setNetworkState(network_.networkManager().state());
        status_service_.setCellularState(network_.cellularManager().state());
        std::uint32_t remaining_ms = kRuntimeMaintenanceDelayMs;
        while (remaining_ms > 0U) {
            const std::uint32_t slice = std::min(remaining_ms, kRuntimeMaintenanceSliceMs);
            vTaskDelay(pdMS_TO_TICKS(slice));
            esp_task_wdt_reset();
            remaining_ms -= slice;
        }
    }
}

void App::runFailedBootLoop() {
    // Without this loop the main task returns and stops feeding TWDT, which
    // panics and reboots after 30 s — turning a recoverable boot failure into
    // a reboot cycle. Just keep TWDT happy and let the operator see the red
    // LED and any logs that did make it out.
    for (;;) {
        std::uint32_t remaining_ms = kRuntimeMaintenanceDelayMs;
        while (remaining_ms > 0U) {
            const std::uint32_t slice = std::min(remaining_ms, kRuntimeMaintenanceSliceMs);
            vTaskDelay(pdMS_TO_TICKS(slice));
            esp_task_wdt_reset();
            remaining_ms -= slice;
        }
    }
}

}  // namespace air360
