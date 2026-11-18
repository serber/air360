#include "air360/app.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstdint>

#include "air360/log_buffer.hpp"
#include "esp_err.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "led_strip.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

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
bool g_ready_in_setup_ap = false;
StaticSemaphore_t g_password_recovered_storage;
SemaphoreHandle_t g_password_recovered = nullptr;

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

void setReadyLed() {
    if (g_ready_in_setup_ap) {
        setLedColor(kLedBrightness, 0U, kLedBrightness / 2U);
    } else {
        setLedColor(0U, kLedBrightness, 0U);
    }
}

void blinkPasswordRecovery() {
    setLedColor(0U, 0U, 0U);
    vTaskDelay(pdMS_TO_TICKS(200));
    setReadyLed();
}

void monitorBootButton(void* context) {
    auto& server = *static_cast<WebServer*>(context);
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_task_wdt_add(nullptr));
    std::int64_t pressed_at = -1;
    bool handled = false;
    for (;;) {
        if (gpio_get_level(GPIO_NUM_0) != 0) {
            pressed_at = -1;
            handled = false;
        } else if (pressed_at < 0) {
            pressed_at = esp_timer_get_time();
        } else if (!handled && esp_timer_get_time() - pressed_at >= 10'000'000) {
            handled = true;
            const auto err = server.queuePasswordRecovery();
            if (err != ESP_OK) {
                ESP_LOGE(kTag, "Could not queue BOOT password recovery: %s", esp_err_to_name(err));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_task_wdt_reset();
    }
}

esp_err_t startBootButtonMonitor(WebServer& server) {
    gpio_config_t button{};
    button.pin_bit_mask = 1ULL << GPIO_NUM_0;
    button.mode = GPIO_MODE_INPUT;
    button.pull_up_en = GPIO_PULLUP_ENABLE;
    const auto err = gpio_config(&button);
    if (err != ESP_OK) {
        return err;
    }
    return xTaskCreate(monitorBootButton, "air360_boot_btn", 3072, &server,
                       tskIDLE_PRIORITY + 1U, nullptr) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
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
    // ESP-IDF pre-initializes TWDT from sdkconfig before app_main()
    // (CONFIG_ESP_TASK_WDT_INIT=y, CONFIG_ESP_TASK_WDT_TIMEOUT_S), so the
    // subscribe below normally succeeds and the fallback init is never reached.
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

    // Power-aware ordering: only low-current kBeforeNetwork sensors (power
    // monitors) run before the radios; the modem, Wi-Fi, BLE, and every other
    // sensor start afterwards so a weak supply is not hit by all loads at once.
    platform_.boot(status_service_);
    data_.bootSensors(status_service_);
    data_.bootBackends(status_service_);
    bootPowerGate();
    network_.bootCellular(platform_, status_service_);
    network_.bootWifi(platform_, status_service_);
    data_.releaseDeferredSensors(platform_, status_service_);
    data_.bootUploads(platform_, network_, status_service_);

    if (!bootWebServer()) {
        runFailedBootLoop();
        return;
    }
    indicateReady();
    const esp_err_t monitor_err = startBootButtonMonitor(web_server_);
    if (monitor_err != ESP_OK) {
        ESP_LOGW(kTag, "BOOT password recovery unavailable: %s", esp_err_to_name(monitor_err));
    }

    // Confirm the running image after the required boot steps succeed.
    // If a freshly-flashed image crashed earlier in boot, the
    // bootloader would have already rolled back; reaching this point means the
    // image is good and we can cancel the pending-verify state so future
    // reboots stay on this slot.
    ota_service_.confirmRunningImage();

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
    ESP_LOGI(kTag, "Boot step 1/12: arm task watchdog");
    const esp_err_t watchdog_err = initWatchdog();
    if (watchdog_err != ESP_OK) {
        ESP_LOGW(kTag, "Watchdog setup failed: %s", esp_err_to_name(watchdog_err));
    } else {
#ifdef CONFIG_ESP_TASK_WDT_PANIC
        constexpr const char* kWdtOnTimeout = "panic";
#else
        constexpr const char* kWdtOnTimeout = "warn only";
#endif
        ESP_LOGI(
            kTag,
            "TWDT: app_main subscribed (%d s, %s)",
            CONFIG_ESP_TASK_WDT_TIMEOUT_S,
            kWdtOnTimeout);
    }
    status_service_.markWatchdogArmed(watchdog_err == ESP_OK);

    ESP_LOGI(kTag, "Boot step 2/12: initialize NVS");
    if (reportBootError("NVS init", initStorage())) {
        return false;
    }
    status_service_.markNvsReady(true);

    ESP_LOGI(kTag, "Boot step 3/12: initialize network core");
    if (reportBootError("Network core init", initNetworkingCore())) {
        return false;
    }

    return true;
}

void App::bootPowerGate() {
    ESP_LOGI(kTag, "Boot step 7/12: evaluate power gate");
    // The sensor task is already running and may update the live config, so
    // evaluate against a consistent snapshot.
    SensorConfigList sensor_config_list{};
    data_.sensorConfigStore().copyTo(sensor_config_list);
    const PowerGateDecision decision = power_gate_.evaluate(
        platform_.deviceConfig(),
        sensor_config_list,
        data_.measurementStore(),
        esp_reset_reason());
    status_service_.setPowerGate(decision);
    if (!decision.sleep_requested) {
        return;
    }

    // Everything started so far is either config in RAM or the low-current
    // power monitors; deep sleep drops all of it and boot restarts from the
    // bootloader when the timer fires.
    ESP_LOGW(
        kTag,
        "Power gate: supply too weak for the radios, deep-sleeping %" PRIu32 " s (%s)",
        decision.sleep_seconds,
        powerGateOutcomeKey(decision.outcome));
    setLedColor(0U, 0U, 0U);
    PowerGate::enterDeepSleep(decision.sleep_seconds);
}

bool App::bootWebServer() {
    ESP_LOGI(kTag, "Boot step 12/12: start status web server");
    g_password_recovered = xSemaphoreCreateBinaryStatic(&g_password_recovered_storage);
    const esp_err_t web_err =
        web_server_.start(
            status_service_,
            network_.networkManager(),
            platform_.deviceConfigRepo(),
            platform_.deviceConfig(),
            data_.sensorConfigStore(),
            data_.sensorManager(),
            data_.measurementStore(),
            data_.backendConfigRepo(),
            platform_.air360Credentials(),
            data_.openSenseMapMappings(),
            data_.backendConfigList(),
            data_.uploadManager(),
            network_.cellularConfigRepo(),
            network_.cellularConfig(),
            ota_service_,
            platform_.deviceConfig().http_port,
            [] { xSemaphoreGive(g_password_recovered); });
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
    g_ready_in_setup_ap = network_.networkManager().state().mode == NetworkMode::kSetupAp;
    setReadyLed();
}

void App::runMaintenanceLoop() {
    for (;;) {
        const NetworkState network_state = network_.networkManager().state();
        if (network_state.hasConnectedUplink() &&
            !network_.networkManager().hasValidTime()) {
            const esp_err_t time_err = network_.networkManager().ensureUplinkTime(10000U);
            if (time_err != ESP_OK) {
                ESP_LOGW(
                    kTag,
                    "Background time sync retry failed: %s",
                    esp_err_to_name(time_err));
            }
        }

        status_service_.setNetworkState(network_.networkManager().state());
        status_service_.setCellularState(network_.cellularManager().state());
        TimeOut_t timeout;
        vTaskSetTimeOutState(&timeout);
        TickType_t remaining = pdMS_TO_TICKS(kRuntimeMaintenanceDelayMs);
        do {
            const TickType_t slice = std::min(remaining, pdMS_TO_TICKS(kRuntimeMaintenanceSliceMs));
            if (xSemaphoreTake(g_password_recovered, slice) == pdTRUE) {
                blinkPasswordRecovery();
            }
            esp_task_wdt_reset();
        } while (xTaskCheckForTimeOut(&timeout, &remaining) == pdFALSE);
    }
}

void App::runFailedBootLoop() {
    // First boot after an OTA (image still PENDING_VERIFY): this image never
    // proved itself, so hand control back to the previous slot instead of
    // parking here with the watchdog fed, which would block the bootloader's
    // rollback forever. Does not return on success.
    const esp_err_t rollback_err = ota_service_.rollBackUnverifiedImage();
    if (rollback_err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(
            kTag,
            "Unverified image could not be rolled back (%s); staying in failed-boot loop",
            esp_err_to_name(rollback_err));
    }

    // Confirmed images (or a failed rollback): without this loop the main
    // task returns and stops feeding TWDT, which panics and reboots after
    // 30 s — turning a recoverable boot failure into a reboot cycle. Just keep
    // TWDT happy and let the operator see the red LED and any logs that did
    // make it out.
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
