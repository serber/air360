#include "air360/data/data_layer.hpp"

#include "air360/config_load_status.hpp"
#include "air360/network/network_layer.hpp"
#include "air360/platform/platform_layer.hpp"
#include "air360/status_service.hpp"
#include "esp_err.h"
#include "esp_log.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.app";

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

}  // namespace

DataLayer::DataLayer()
    : sensor_config_list_(makeDefaultSensorConfigList()),
      backend_config_list_(makeDefaultBackendConfigList()) {}

void DataLayer::bootSensors(PlatformLayer& platform, StatusService& status_service) {
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
    status_service.recordConfigLoad(
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
    status_service.setSensors(sensor_manager_);
    status_service.setMeasurements(measurement_store_);

    ble_advertiser_.start(platform.deviceConfig(), measurement_store_);
    status_service.setBleAdvertiser(ble_advertiser_);
}

void DataLayer::bootBackends(StatusService& status_service) {
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
    status_service.recordConfigLoad(
        ConfigRepositoryKind::kBackends,
        backend_config_source,
        backend_config_err,
        backend_defaults_written);
    ESP_LOGI(
        kTag,
        "Backend config source: %s; wrote defaults: %s",
        configLoadSourceLabel(backend_config_source),
        backend_defaults_written ? "yes" : "no");
}

void DataLayer::bootUploads(
    PlatformLayer& platform,
    NetworkLayer& network,
    StatusService& status_service) {
    ESP_LOGI(kTag, "Boot step 8/9: start upload manager");
    upload_manager_.start(
        platform.buildInfo(),
        platform.deviceConfig(),
        sensor_manager_,
        measurement_store_,
        network.networkManager(),
        platform.air360Credentials());
    const esp_err_t upload_apply_err = upload_manager_.applyConfig(backend_config_list_);
    if (upload_apply_err != ESP_OK) {
        ESP_LOGW(
            kTag,
            "Upload manager apply failed: %s",
            esp_err_to_name(upload_apply_err));
    }
    status_service.setUploads(upload_manager_);
}

}  // namespace air360
