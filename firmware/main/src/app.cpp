#include "air360/app.hpp"

#include <cinttypes>
#include <cstdint>

#include "air360/build_info.hpp"
#include "air360/config_repository.hpp"
#include "air360/network_manager.hpp"
#include "air360/status_service.hpp"
#include "air360/sensors/sensor_config_repository.hpp"
#include "air360/sensors/sensor_manager.hpp"
#include "air360/cellular_config_repository.hpp"
#include "air360/cellular_manager.hpp"
#include "air360/uploads/backend_config_repository.hpp"
#include "air360/uploads/measurement_store.hpp"
#include "air360/uploads/upload_manager.hpp"
#include "air360/web_server.hpp"
#include "driver/gpio.h"
#include "esp_err.h"
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
constexpr gpio_num_t kGreenLedGpio = GPIO_NUM_11;
constexpr gpio_num_t kRedLedGpio = GPIO_NUM_10;
constexpr TickType_t kRuntimeMaintenanceDelay = pdMS_TO_TICKS(10000);

void setBootLedState(bool green_on, bool red_on) {
    gpio_set_level(kGreenLedGpio, green_on ? 1 : 0);
    gpio_set_level(kRedLedGpio, red_on ? 1 : 0);
}

esp_err_t initBootLeds() {
    gpio_config_t config{};
    config.pin_bit_mask =
        (1ULL << static_cast<unsigned>(kGreenLedGpio)) |
        (1ULL << static_cast<unsigned>(kRedLedGpio));
    config.mode = GPIO_MODE_OUTPUT;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;

    const esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) {
        return err;
    }

    setBootLedState(false, false);
    return ESP_OK;
}

bool hasStationConfig(const DeviceConfig& config) {
    return config.wifi_sta_ssid[0] != '\0';
}

void debugWindowCallback(void* arg) {
    auto* nm = static_cast<NetworkManager*>(arg);
    ESP_LOGI(kTag, "Wi-Fi debug window expired, stopping station");
    const esp_err_t err = nm->stopStation();
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Station stop after debug window failed: %s", esp_err_to_name(err));
    }
}

esp_err_t initWatchdog() {
    // ESP-IDF may pre-initialize TWDT from sdkconfig before app_main().
    esp_err_t err = esp_task_wdt_add(nullptr);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        return ESP_OK;
    }

    esp_task_wdt_config_t config{};
    config.timeout_ms = 10000;
    config.idle_core_mask = (1U << portNUM_PROCESSORS) - 1U;
    config.trigger_panic = false;

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

void App::run() {
    // Keep long-lived runtime objects out of the small ESP-IDF main task stack.
    static BuildInfo build_info = getBuildInfo();
    static ConfigRepository config_repository;
    static DeviceConfig config = makeDefaultDeviceConfig();
    static StatusService status_service(build_info);
    static SensorConfigRepository sensor_config_repository;
    static SensorConfigList sensor_config_list = makeDefaultSensorConfigList();
    static SensorManager sensor_manager;
    static MeasurementStore measurement_store;
    static CellularConfigRepository cellular_config_repository;
    static CellularConfig cellular_config = makeDefaultCellularConfig();
    static CellularManager cellular_manager;
    static BackendConfigRepository backend_config_repository;
    static BackendConfigList backend_config_list = makeDefaultBackendConfigList();
    static UploadManager upload_manager;
    static NetworkManager network_manager;
    static WebServer web_server;
    static esp_timer_handle_t debug_window_timer = nullptr;

    const esp_err_t leds_err = initBootLeds();
    if (leds_err != ESP_OK) {
        ESP_LOGW(kTag, "Boot LED setup failed: %s", esp_err_to_name(leds_err));
    }

    ESP_LOGI(kTag, "Boot step 1/9: arm task watchdog");
    const esp_err_t watchdog_err = initWatchdog();
    if (watchdog_err != ESP_OK) {
        ESP_LOGW(kTag, "Watchdog setup failed: %s", esp_err_to_name(watchdog_err));
    }

    ESP_LOGI(kTag, "Boot step 2/9: initialize NVS");
    const esp_err_t storage_err = initStorage();
    if (storage_err != ESP_OK) {
        ESP_LOGE(kTag, "NVS init failed: %s", esp_err_to_name(storage_err));
        setBootLedState(false, true);
        return;
    }

    ESP_LOGI(kTag, "Boot step 3/9: initialize network core");
    const esp_err_t network_core_err = initNetworkingCore();
    if (network_core_err != ESP_OK) {
        ESP_LOGE(
            kTag,
            "Network core init failed: %s",
            esp_err_to_name(network_core_err));
        setBootLedState(false, true);
        return;
    }

    config = makeDefaultDeviceConfig();
    bool loaded_from_storage = false;
    bool wrote_defaults = false;

    ESP_LOGI(kTag, "Boot step 4/9: load or create device config");
    esp_err_t config_err =
        config_repository.loadOrCreate(config, loaded_from_storage, wrote_defaults);
    if (config_err != ESP_OK) {
        ESP_LOGW(
            kTag,
            "Config load failed, using in-memory defaults: %s",
            esp_err_to_name(config_err));
        config = makeDefaultDeviceConfig();
    }

    std::uint32_t boot_count = 0;
    const esp_err_t boot_count_err = config_repository.incrementBootCount(boot_count);
    if (boot_count_err != ESP_OK) {
        ESP_LOGW(
            kTag,
            "Boot counter update failed: %s",
            esp_err_to_name(boot_count_err));
    }

    status_service.markWatchdogArmed(watchdog_err == ESP_OK);
    status_service.markNvsReady(true);
    status_service.setConfig(config, loaded_from_storage, wrote_defaults);
    status_service.setBootCount(boot_count);

    cellular_config = makeDefaultCellularConfig();
    bool cellular_config_loaded = false;
    bool cellular_defaults_written = false;

    ESP_LOGI(kTag, "Boot step 4b/9: load or create cellular config");
    const esp_err_t cellular_config_err = cellular_config_repository.loadOrCreate(
        cellular_config,
        cellular_config_loaded,
        cellular_defaults_written);
    if (cellular_config_err != ESP_OK) {
        ESP_LOGW(
            kTag,
            "Cellular config load failed, using in-memory defaults: %s",
            esp_err_to_name(cellular_config_err));
        cellular_config = makeDefaultCellularConfig();
    }
    static_cast<void>(cellular_config_loaded);
    static_cast<void>(cellular_defaults_written);
    ESP_LOGI(
        kTag,
        "Cellular uplink: %s",
        cellular_config.enabled ? "enabled" : "disabled");

    cellular_manager.init(network_manager);
    cellular_manager.start(cellular_config);

    sensor_config_list = makeDefaultSensorConfigList();
    bool sensor_config_loaded = false;
    bool sensor_defaults_written = false;

    ESP_LOGI(kTag, "Boot step 5/9: load or create sensor config");
    const esp_err_t sensor_config_err = sensor_config_repository.loadOrCreate(
        sensor_config_list,
        sensor_config_loaded,
        sensor_defaults_written);
    if (sensor_config_err != ESP_OK) {
        ESP_LOGW(
            kTag,
            "Sensor config load failed, using in-memory defaults: %s",
            esp_err_to_name(sensor_config_err));
        sensor_config_list = makeDefaultSensorConfigList();
    }
    static_cast<void>(sensor_config_loaded);
    static_cast<void>(sensor_defaults_written);

    sensor_manager.setMeasurementStore(measurement_store);
    sensor_manager.applyConfig(sensor_config_list);
    status_service.setSensors(sensor_manager);
    status_service.setMeasurements(measurement_store);

    backend_config_list = makeDefaultBackendConfigList();
    bool backend_config_loaded = false;
    bool backend_defaults_written = false;

    ESP_LOGI(kTag, "Boot step 6/9: load or create backend config");
    const esp_err_t backend_config_err = backend_config_repository.loadOrCreate(
        backend_config_list,
        backend_config_loaded,
        backend_defaults_written);
    if (backend_config_err != ESP_OK) {
        ESP_LOGW(
            kTag,
            "Backend config load failed, using in-memory defaults: %s",
            esp_err_to_name(backend_config_err));
        backend_config_list = makeDefaultBackendConfigList();
    }
    static_cast<void>(backend_config_loaded);
    static_cast<void>(backend_defaults_written);

    ESP_LOGI(kTag, "Boot step 7/9: resolve network mode");
    if (cellular_config.enabled != 0U) {
        // Cellular is the primary uplink.  Wi-Fi station is started only if
        // credentials exist, giving the operator a debug window at boot.
        // No AP fallback here — if cellular also fails, the fallback cascade
        // is driven by CellularManager (Phase 1).
        if (hasStationConfig(config)) {
            const esp_err_t station_err = network_manager.connectStation(config);
            if (station_err != ESP_OK) {
                ESP_LOGW(
                    kTag,
                    "Station join failed during cellular debug window: %s",
                    esp_err_to_name(station_err));
            }

            if (cellular_config.wifi_debug_window_s > 0U && debug_window_timer == nullptr) {
                esp_timer_create_args_t timer_args{};
                timer_args.callback = debugWindowCallback;
                timer_args.arg = &network_manager;
                timer_args.name = "wifi_dbg_win";
                const esp_err_t timer_err =
                    esp_timer_create(&timer_args, &debug_window_timer);
                if (timer_err == ESP_OK) {
                    const std::uint64_t window_us =
                        static_cast<std::uint64_t>(cellular_config.wifi_debug_window_s) *
                        1000000ULL;
                    esp_timer_start_once(debug_window_timer, window_us);
                    ESP_LOGI(
                        kTag,
                        "Wi-Fi debug window active (%" PRIu16 " s)",
                        cellular_config.wifi_debug_window_s);
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
        if (hasStationConfig(config)) {
            ESP_LOGI(kTag, "Station config present, attempting normal mode Wi-Fi join");
            const esp_err_t station_err = network_manager.connectStation(config);
            if (station_err != ESP_OK) {
                ESP_LOGW(
                    kTag,
                    "Station join failed, falling back to setup AP: %s",
                    esp_err_to_name(station_err));
                const esp_err_t ap_err = network_manager.startLabAp(config);
                if (ap_err != ESP_OK) {
                    ESP_LOGW(kTag, "Setup AP start failed: %s", esp_err_to_name(ap_err));
                }
            }
        } else {
            ESP_LOGI(kTag, "No station config present, entering setup AP mode");
            const esp_err_t ap_err = network_manager.startLabAp(config);
            if (ap_err != ESP_OK) {
                ESP_LOGW(kTag, "Setup AP start failed: %s", esp_err_to_name(ap_err));
            }
        }
    }
    status_service.setNetworkState(network_manager.state());
    status_service.setCellularState(cellular_manager.state());

    ESP_LOGI(kTag, "Boot step 8/9: start upload manager");
    upload_manager.start(build_info, config, sensor_manager, measurement_store, network_manager);
    upload_manager.applyConfig(backend_config_list);
    status_service.setUploads(upload_manager);

    ESP_LOGI(kTag, "Boot step 9/9: start status web server");
    const esp_err_t web_err =
        web_server.start(
            status_service,
            network_manager,
            config_repository,
            config,
            sensor_config_repository,
            sensor_config_list,
            sensor_manager,
            measurement_store,
            backend_config_repository,
            backend_config_list,
            upload_manager,
            cellular_config_repository,
            cellular_config,
            config.http_port);
    if (web_err != ESP_OK) {
        ESP_LOGE(kTag, "Web server start failed: %s", esp_err_to_name(web_err));
        setBootLedState(false, true);
        return;
    }
    status_service.setWebServerStarted(true);

    ESP_LOGI(
        kTag,
        "Runtime ready on port %" PRIu16,
        config.http_port);
    setBootLedState(true, false);

    esp_task_wdt_delete(nullptr);

    for (;;) {
        const NetworkState network_state = network_manager.state();
        if (network_state.mode == NetworkMode::kStation &&
            network_state.station_connected &&
            !network_manager.hasValidTime()) {
            const esp_err_t time_err = network_manager.ensureStationTime(10000U);
            if (time_err != ESP_OK) {
                ESP_LOGW(
                    kTag,
                    "Background time sync retry failed: %s",
                    esp_err_to_name(time_err));
            }
        }

        status_service.setNetworkState(network_manager.state());
        status_service.setCellularState(cellular_manager.state());
        vTaskDelay(kRuntimeMaintenanceDelay);
    }
}

}  // namespace air360
