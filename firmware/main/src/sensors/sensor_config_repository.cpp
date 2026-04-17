#include "air360/sensors/sensor_config_repository.hpp"

#include <cstddef>
#include <string>

#include "air360/sensors/sensor_registry.hpp"
#include "esp_log.h"
#include "nvs.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.sensor_cfg";
constexpr char kNamespace[] = "air360";
constexpr char kConfigKey[] = "sensor_cfg";

esp_err_t saveInternal(nvs_handle_t handle, const SensorConfigList& config) {
    esp_err_t err = nvs_set_blob(handle, kConfigKey, &config, sizeof(config));
    if (err != ESP_OK) {
        return err;
    }

    return nvs_commit(handle);
}

bool hasValidShape(const SensorConfigList& config) {
    return config.magic == kSensorConfigMagic &&
           config.record_size == static_cast<std::uint16_t>(sizeof(SensorRecord)) &&
           config.sensor_count <= kMaxConfiguredSensors &&
           config.next_sensor_id != 0U;
}

}  // namespace

bool SensorConfigRepository::isValid(const SensorConfigList& config) const {
    if (!hasValidShape(config) || config.schema_version != kSensorConfigSchemaVersion) {
        return false;
    }

    SensorRegistry registry;
    for (std::size_t index = 0; index < config.sensor_count; ++index) {
        std::string error;
        if (!registry.validateRecord(config.sensors[index], error)) {
            return false;
        }
    }

    return true;
}

esp_err_t SensorConfigRepository::loadOrCreate(
    SensorConfigList& out_config,
    bool& loaded_from_storage,
    bool& wrote_defaults) {
    loaded_from_storage = false;
    wrote_defaults = false;

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    std::size_t blob_size = 0U;
    err = nvs_get_blob(handle, kConfigKey, nullptr, &blob_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(kTag, "No stored sensor config found, writing defaults");
        out_config = makeDefaultSensorConfigList();
        err = saveInternal(handle, out_config);
        if (err == ESP_OK) {
            wrote_defaults = true;
        }
        nvs_close(handle);
        return err;
    }

    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    if (blob_size == sizeof(SensorConfigList)) {
        SensorConfigList loaded{};
        err = nvs_get_blob(handle, kConfigKey, &loaded, &blob_size);
        if (err != ESP_OK) {
            nvs_close(handle);
            return err;
        }

        if (blob_size == sizeof(SensorConfigList) && isValid(loaded)) {
            out_config = loaded;
            loaded_from_storage = true;
            nvs_close(handle);
            return ESP_OK;
        }
    }

    ESP_LOGW(kTag, "Stored sensor config invalid or incompatible, replacing with defaults");
    out_config = makeDefaultSensorConfigList();
    err = saveInternal(handle, out_config);
    if (err == ESP_OK) {
        wrote_defaults = true;
    }
    nvs_close(handle);
    return err;
}

esp_err_t SensorConfigRepository::save(const SensorConfigList& config) {
    if (!isValid(config)) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = saveInternal(handle, config);
    nvs_close(handle);
    return err;
}

}  // namespace air360
