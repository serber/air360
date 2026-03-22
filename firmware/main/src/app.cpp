#include "air360/app.hpp"

#include <cinttypes>
#include <cstdint>

#include "air360/build_info.hpp"
#include "air360/config_repository.hpp"
#include "air360/network_manager.hpp"
#include "air360/status_service.hpp"
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
    ESP_LOGI(kTag, "Boot step 1/6: arm task watchdog");
    const esp_err_t watchdog_err = initWatchdog();
    if (watchdog_err != ESP_OK) {
        ESP_LOGW(kTag, "Watchdog setup failed: %s", esp_err_to_name(watchdog_err));
    }

    ESP_LOGI(kTag, "Boot step 2/6: initialize NVS");
    const esp_err_t storage_err = initStorage();
    if (storage_err != ESP_OK) {
        ESP_LOGE(kTag, "NVS init failed: %s", esp_err_to_name(storage_err));
        return;
    }

    ESP_LOGI(kTag, "Boot step 3/6: initialize network core");
    const esp_err_t network_core_err = initNetworkingCore();
    if (network_core_err != ESP_OK) {
        ESP_LOGE(
            kTag,
            "Network core init failed: %s",
            esp_err_to_name(network_core_err));
        return;
    }

    ConfigRepository config_repository;
    DeviceConfig config = makeDefaultDeviceConfig();
    bool loaded_from_storage = false;
    bool wrote_defaults = false;

    ESP_LOGI(kTag, "Boot step 4/6: load or create device config");
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

    StatusService status_service(getBuildInfo());
    status_service.markWatchdogArmed(watchdog_err == ESP_OK);
    status_service.markNvsReady(true);
    status_service.setConfig(config, loaded_from_storage, wrote_defaults);
    status_service.setBootCount(boot_count);

    NetworkManager network_manager;
    if (config.lab_ap_enabled != 0U) {
        ESP_LOGI(kTag, "Boot step 5/6: start lab AP bring-up hook");
        const esp_err_t ap_err = network_manager.startLabAp(config);
        if (ap_err != ESP_OK) {
            ESP_LOGW(kTag, "Lab AP start failed: %s", esp_err_to_name(ap_err));
        }
        status_service.setNetworkState(network_manager.state());
    } else {
        ESP_LOGI(kTag, "Boot step 5/6: lab AP disabled in config");
    }

    ESP_LOGI(kTag, "Boot step 6/6: start status web server");
    WebServer web_server;
    const esp_err_t web_err = web_server.start(status_service, config.http_port);
    if (web_err != ESP_OK) {
        ESP_LOGE(kTag, "Web server start failed: %s", esp_err_to_name(web_err));
        return;
    }
    status_service.setWebServerStarted(true);

    ESP_LOGI(
        kTag,
        "Phase 1 runtime ready on port %" PRIu16,
        config.http_port);

    esp_task_wdt_delete(nullptr);

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}

}  // namespace air360
