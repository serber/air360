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
#include "esp_timer.h"
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

bool hasStationConfig(const DeviceConfig& config) {
    return config.wifi_sta_ssid[0] != '\0';
}

ConfigLoadSource resolveConfigLoadSource(bool loaded_from_storage) {
    return loaded_from_storage ? ConfigLoadSource::kNvsPrimary : ConfigLoadSource::kDefaults;
}

const char* configLoadSourceLabel(ConfigLoadSource source) {
    switch (source) {
        case ConfigLoadSource::kNvsPrimary:
            return "NVS primary";
        case ConfigLoadSource::kNvsBackup:
            return "NVS backup";
        case ConfigLoadSource::kDefaults:
        default:
            return "defaults";
    }
}

void debugWindowCallback(void* arg) {
    auto* nm = static_cast<NetworkManager*>(arg);
    if (nm != nullptr) {
        nm->requestStopStation();
    }
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

}  // namespace

App::App()
    : build_info_(getBuildInfo()),
      config_(makeDefaultDeviceConfig()),
      status_service_(build_info_),
      sensor_config_list_(makeDefaultSensorConfigList()),
      cellular_config_(makeDefaultCellularConfig()),
      backend_config_list_(makeDefaultBackendConfigList()) {}

void App::run() {
    logBufferInstall();

    const esp_err_t leds_err = initRgbLed();
    if (leds_err != ESP_OK) {
        ESP_LOGW(kTag, "RGB LED setup failed: %s", esp_err_to_name(leds_err));
    }

    ESP_LOGI(kTag, "Boot step 1/9: arm task watchdog");
    const esp_err_t watchdog_err = initWatchdog();
    if (watchdog_err != ESP_OK) {
        ESP_LOGW(kTag, "Watchdog setup failed: %s", esp_err_to_name(watchdog_err));
    } else {
        ESP_LOGI(kTag, "TWDT: app_main subscribed (30 s, panic enabled)");
    }

    ESP_LOGI(kTag, "Boot step 2/9: initialize NVS");
    const esp_err_t storage_err = initStorage();
    if (storage_err != ESP_OK) {
        ESP_LOGE(kTag, "NVS init failed: %s", esp_err_to_name(storage_err));
        setLedColor(kLedBrightness, 0U, 0U);
        return;
    }

    ESP_LOGI(kTag, "Boot step 3/9: initialize network core");
    const esp_err_t network_core_err = initNetworkingCore();
    if (network_core_err != ESP_OK) {
        ESP_LOGE(
            kTag,
            "Network core init failed: %s",
            esp_err_to_name(network_core_err));
        setLedColor(kLedBrightness, 0U, 0U);
        return;
    }

    config_ = makeDefaultDeviceConfig();
    bool loaded_from_storage = false;
    bool wrote_defaults = false;

    ESP_LOGI(kTag, "Boot step 4/9: load or create device config");
    esp_err_t config_err =
        config_repository_.loadOrCreate(config_, loaded_from_storage, wrote_defaults);
    if (config_err != ESP_OK) {
        ESP_LOGW(
            kTag,
            "Config load failed, using in-memory defaults: %s",
            esp_err_to_name(config_err));
        config_ = makeDefaultDeviceConfig();
    }
    const ConfigLoadSource device_config_source = resolveConfigLoadSource(loaded_from_storage);
    ESP_LOGI(
        kTag,
        "Device config source: %s; wrote defaults: %s",
        configLoadSourceLabel(device_config_source),
        wrote_defaults ? "yes" : "no");

    std::uint32_t boot_count = 0;
    const esp_err_t boot_count_err = config_repository_.incrementBootCount(boot_count);
    if (boot_count_err != ESP_OK) {
        ESP_LOGW(
            kTag,
            "Boot counter update failed: %s",
            esp_err_to_name(boot_count_err));
    }

    status_service_.markWatchdogArmed(watchdog_err == ESP_OK);
    status_service_.markNvsReady(true);
    status_service_.setConfig(config_, loaded_from_storage, wrote_defaults);
    status_service_.recordConfigLoad(
        ConfigRepositoryKind::kDevice,
        device_config_source,
        config_err,
        wrote_defaults);
    status_service_.setBootCount(boot_count);

    cellular_config_ = makeDefaultCellularConfig();
    bool cellular_config_loaded = false;
    bool cellular_defaults_written = false;

    ESP_LOGI(kTag, "Boot step 4b/9: load or create cellular config");
    const esp_err_t cellular_config_err = cellular_config_repository_.loadOrCreate(
        cellular_config_,
        cellular_config_loaded,
        cellular_defaults_written);
    if (cellular_config_err != ESP_OK) {
        ESP_LOGW(
            kTag,
            "Cellular config load failed, using in-memory defaults: %s",
            esp_err_to_name(cellular_config_err));
        cellular_config_ = makeDefaultCellularConfig();
    }
    const ConfigLoadSource cellular_config_source =
        resolveConfigLoadSource(cellular_config_loaded);
    status_service_.recordConfigLoad(
        ConfigRepositoryKind::kCellular,
        cellular_config_source,
        cellular_config_err,
        cellular_defaults_written);
    ESP_LOGI(
        kTag,
        "Cellular config source: %s; wrote defaults: %s",
        configLoadSourceLabel(cellular_config_source),
        cellular_defaults_written ? "yes" : "no");
    ESP_LOGI(
        kTag,
        "Cellular uplink: %s",
        cellular_config_.enabled ? "enabled" : "disabled");

    cellular_manager_.init(network_manager_);
    cellular_manager_.start(cellular_config_);
    status_service_.setCellularManager(cellular_manager_);

    sensor_config_list_ = makeDefaultSensorConfigList();
    bool sensor_config_loaded = false;
    bool sensor_defaults_written = false;

    ESP_LOGI(kTag, "Boot step 5/9: load or create sensor config");
    const esp_err_t sensor_config_err = sensor_config_repository_.loadOrCreate(
        sensor_config_list_,
        sensor_config_loaded,
        sensor_defaults_written);
    if (sensor_config_err != ESP_OK) {
        ESP_LOGW(
            kTag,
            "Sensor config load failed, using in-memory defaults: %s",
            esp_err_to_name(sensor_config_err));
        sensor_config_list_ = makeDefaultSensorConfigList();
    }
    const ConfigLoadSource sensor_config_source = resolveConfigLoadSource(sensor_config_loaded);
    status_service_.recordConfigLoad(
        ConfigRepositoryKind::kSensors,
        sensor_config_source,
        sensor_config_err,
        sensor_defaults_written);
    ESP_LOGI(
        kTag,
        "Sensor config source: %s; wrote defaults: %s",
        configLoadSourceLabel(sensor_config_source),
        sensor_defaults_written ? "yes" : "no");

    sensor_manager_.setMeasurementStore(measurement_store_);
    const esp_err_t sensor_apply_err = sensor_manager_.applyConfig(sensor_config_list_);
    if (sensor_apply_err != ESP_OK) {
        ESP_LOGW(
            kTag,
            "Sensor manager apply failed: %s",
            esp_err_to_name(sensor_apply_err));
    }
    status_service_.setSensors(sensor_manager_);
    status_service_.setMeasurements(measurement_store_);

    ble_advertiser_.start(config_, measurement_store_);
    status_service_.setBleAdvertiser(ble_advertiser_);

    backend_config_list_ = makeDefaultBackendConfigList();
    bool backend_config_loaded = false;
    bool backend_defaults_written = false;

    ESP_LOGI(kTag, "Boot step 6/9: load or create backend config");
    const esp_err_t backend_config_err = backend_config_repository_.loadOrCreate(
        backend_config_list_,
        backend_config_loaded,
        backend_defaults_written);
    if (backend_config_err != ESP_OK) {
        ESP_LOGW(
            kTag,
            "Backend config load failed, using in-memory defaults: %s",
            esp_err_to_name(backend_config_err));
        backend_config_list_ = makeDefaultBackendConfigList();
    }
    const ConfigLoadSource backend_config_source = resolveConfigLoadSource(backend_config_loaded);
    status_service_.recordConfigLoad(
        ConfigRepositoryKind::kBackends,
        backend_config_source,
        backend_config_err,
        backend_defaults_written);
    ESP_LOGI(
        kTag,
        "Backend config source: %s; wrote defaults: %s",
        configLoadSourceLabel(backend_config_source),
        backend_defaults_written ? "yes" : "no");

    ESP_LOGI(kTag, "Boot step 7/9: resolve network mode");
    if (cellular_config_.enabled != 0U) {
        // Cellular is the primary uplink.  Wi-Fi station is started only if
        // credentials exist, giving the operator a debug window at boot.
        // No AP fallback here — if cellular also fails, the fallback cascade
        // is driven by CellularManager (Phase 1).
        if (hasStationConfig(config_)) {
            const esp_err_t station_err = network_manager_.connectStation(config_);
            if (station_err != ESP_OK) {
                ESP_LOGW(
                    kTag,
                    "Station join failed during cellular debug window: %s",
                    esp_err_to_name(station_err));
            }

            if (cellular_config_.wifi_debug_window_s > 0U && debug_window_timer_ == nullptr) {
                esp_timer_create_args_t timer_args{};
                timer_args.callback = debugWindowCallback;
                timer_args.arg = &network_manager_;
                timer_args.name = "wifi_dbg_win";
                const esp_err_t timer_err =
                    esp_timer_create(&timer_args, &debug_window_timer_);
                if (timer_err == ESP_OK) {
                    const std::uint64_t window_us =
                        static_cast<std::uint64_t>(cellular_config_.wifi_debug_window_s) *
                        1000000ULL;
                    esp_timer_start_once(debug_window_timer_, window_us);
                    ESP_LOGI(
                        kTag,
                        "Wi-Fi debug window active (%" PRIu16 " s)",
                        cellular_config_.wifi_debug_window_s);
                } else {
                    ESP_LOGW(
                        kTag,
                        "Failed to create debug window timer: %s",
                        esp_err_to_name(timer_err));
                }
            }
        } else {
            ESP_LOGI(kTag, "Cellular uplink, no station credentials — skipping Wi-Fi");
        }
    } else {
        // Cellular disabled — standard Wi-Fi / setup-AP flow.
        if (hasStationConfig(config_)) {
            ESP_LOGI(kTag, "Station config present, attempting normal mode Wi-Fi join");
            const esp_err_t station_err = network_manager_.connectStation(config_);
            if (station_err != ESP_OK) {
                ESP_LOGW(
                    kTag,
                    "Station join failed, falling back to setup AP: %s",
                    esp_err_to_name(station_err));
                const esp_err_t ap_err = network_manager_.startLabAp(config_);
                if (ap_err != ESP_OK) {
                    ESP_LOGW(kTag, "Setup AP start failed: %s", esp_err_to_name(ap_err));
                }
            }
        } else {
            ESP_LOGI(kTag, "No station config present, entering setup AP mode");
            const esp_err_t ap_err = network_manager_.startLabAp(config_);
            if (ap_err != ESP_OK) {
                ESP_LOGW(kTag, "Setup AP start failed: %s", esp_err_to_name(ap_err));
            }
        }
    }
    status_service_.setNetworkState(network_manager_.state());
    status_service_.setCellularState(cellular_manager_.state());

    ESP_LOGI(kTag, "Boot step 8/9: start upload manager");
    upload_manager_.start(
        build_info_,
        config_,
        sensor_manager_,
        measurement_store_,
        network_manager_,
        air360_api_credentials_);
    const esp_err_t upload_apply_err = upload_manager_.applyConfig(backend_config_list_);
    if (upload_apply_err != ESP_OK) {
        ESP_LOGW(
            kTag,
            "Upload manager apply failed: %s",
            esp_err_to_name(upload_apply_err));
    }
    status_service_.setUploads(upload_manager_);

    ESP_LOGI(kTag, "Boot step 9/9: start status web server");
    const esp_err_t web_err =
        web_server_.start(
            status_service_,
            network_manager_,
            config_repository_,
            config_,
            sensor_config_repository_,
            sensor_config_list_,
            sensor_manager_,
            measurement_store_,
            backend_config_repository_,
            air360_api_credentials_,
            backend_config_list_,
            upload_manager_,
            cellular_config_repository_,
            cellular_config_,
            config_.http_port);
    if (web_err != ESP_OK) {
        ESP_LOGE(kTag, "Web server start failed: %s", esp_err_to_name(web_err));
        setLedColor(kLedBrightness, 0U, 0U);
        return;
    }
    status_service_.setWebServerStarted(true);

    ESP_LOGI(
        kTag,
        "Runtime ready on port %" PRIu16,
        config_.http_port);
    if (network_manager_.state().mode == NetworkMode::kSetupAp) {
        setLedColor(kLedBrightness, 0U, kLedBrightness / 2U);  // pink — AP mode
    } else {
        setLedColor(0U, kLedBrightness, 0U);  // green — station mode
    }

    for (;;) {
        const NetworkState network_state = network_manager_.state();
        if (network_state.mode == NetworkMode::kStation &&
            network_state.station_connected &&
            !network_manager_.hasValidTime()) {
            const esp_err_t time_err = network_manager_.ensureStationTime(10000U);
            if (time_err != ESP_OK) {
                ESP_LOGW(
                    kTag,
                    "Background time sync retry failed: %s",
                    esp_err_to_name(time_err));
            }
        }

        status_service_.setNetworkState(network_manager_.state());
        status_service_.setCellularState(cellular_manager_.state());
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
