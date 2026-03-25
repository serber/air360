#include "air360/app.hpp"

#include <cinttypes>
#include <cstdint>

#include "air360/build_info.hpp"
#include "air360/config_repository.hpp"
#include "air360/network_manager.hpp"
#include "air360/status_service.hpp"
#include "air360/sensors/sensor_config_repository.hpp"
#include "air360/sensors/sensor_manager.hpp"
#include "air360/web_server.hpp"
#include "esp_err.h"
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

bool hasStationConfig(const DeviceConfig& config) {
    return config.wifi_sta_ssid[0] != '\0';
}

esp_err_t initWatchdog() {
    esp_task_wdt_config_t config{};
    config.timeout_ms = 10000;
    config.idle_core_mask = (1U << portNUM_PROCESSORS) - 1U;
    config.trigger_panic = false;

    esp_err_t err = esp_task_wdt_init(&config);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
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
    static ConfigRepository config_repository;
    static DeviceConfig config = makeDefaultDeviceConfig();
    static StatusService status_service(getBuildInfo());
    static SensorConfigRepository sensor_config_repository;
    static SensorConfigList sensor_config_list = makeDefaultSensorConfigList();
    static SensorManager sensor_manager;
    static NetworkManager network_manager;
    static WebServer web_server;

    ESP_LOGI(kTag, "Boot step 1/7: arm task watchdog");
    const esp_err_t watchdog_err = initWatchdog();
    if (watchdog_err != ESP_OK) {
        ESP_LOGW(kTag, "Watchdog setup failed: %s", esp_err_to_name(watchdog_err));
    }

    ESP_LOGI(kTag, "Boot step 2/7: initialize NVS");
    const esp_err_t storage_err = initStorage();
    if (storage_err != ESP_OK) {
        ESP_LOGE(kTag, "NVS init failed: %s", esp_err_to_name(storage_err));
        return;
    }

    ESP_LOGI(kTag, "Boot step 3/7: initialize network core");
    const esp_err_t network_core_err = initNetworkingCore();
    if (network_core_err != ESP_OK) {
        ESP_LOGE(
            kTag,
            "Network core init failed: %s",
            esp_err_to_name(network_core_err));
        return;
    }

    config = makeDefaultDeviceConfig();
    bool loaded_from_storage = false;
    bool wrote_defaults = false;

    ESP_LOGI(kTag, "Boot step 4/7: load or create device config");
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

    sensor_config_list = makeDefaultSensorConfigList();
    bool sensor_config_loaded = false;
    bool sensor_defaults_written = false;

    ESP_LOGI(kTag, "Boot step 5/7: load or create sensor config");
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

    sensor_manager.applyConfig(sensor_config_list);
    status_service.setSensors(sensor_manager);

    ESP_LOGI(kTag, "Boot step 6/7: resolve network mode");
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
    status_service.setNetworkState(network_manager.state());

    ESP_LOGI(kTag, "Boot step 7/7: start status web server");
    const esp_err_t web_err =
        web_server.start(
            status_service,
            config_repository,
            config,
            sensor_config_repository,
            sensor_config_list,
            sensor_manager,
            config.http_port);
    if (web_err != ESP_OK) {
        ESP_LOGE(kTag, "Web server start failed: %s", esp_err_to_name(web_err));
        return;
    }
    status_service.setWebServerStarted(true);

    ESP_LOGI(
        kTag,
        "Phase 3.1 runtime ready on port %" PRIu16,
        config.http_port);

    esp_task_wdt_delete(nullptr);

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}

}  // namespace air360
