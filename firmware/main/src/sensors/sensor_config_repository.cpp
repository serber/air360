#include "air360/sensors/sensor_config_repository.hpp"

#include <array>
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

[[nodiscard]] esp_err_t saveInternal(nvs_handle_t handle, const SensorConfigList& config) {
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

// Sensor types whose drivers were removed. Records of these types are dropped
// on load; otherwise one retired sensor would fail validation and reset the
// whole stored list to defaults.
constexpr std::array<SensorType, 1U> kRetiredSensorTypes{{
    SensorType::kMe3No2,
}};

bool isRetiredSensorType(SensorType type) {
    for (const SensorType retired : kRetiredSensorTypes) {
        if (type == retired) {
            return true;
        }
    }
    return false;
}

// Requires hasValidShape(config) so sensor_count is within the array bounds.
std::size_t removeRetiredSensors(SensorConfigList& config) {
    std::size_t next_index = 0U;
    for (std::size_t index = 0; index < config.sensor_count; ++index) {
        if (isRetiredSensorType(config.sensors[index].sensor_type)) {
            continue;
        }
        if (next_index != index) {
            config.sensors[next_index] = config.sensors[index];
        }
        ++next_index;
    }

    const std::size_t removed = config.sensor_count - next_index;
    for (std::size_t index = next_index; index < config.sensors.size(); ++index) {
        config.sensors[index] = SensorRecord{};
    }
    config.sensor_count = static_cast<std::uint16_t>(next_index);
    return removed;
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

        std::size_t removed_count = 0U;
        if (blob_size == sizeof(SensorConfigList) && hasValidShape(loaded)) {
            removed_count = removeRetiredSensors(loaded);
        }

        if (blob_size == sizeof(SensorConfigList) && isValid(loaded)) {
            out_config = loaded;
            loaded_from_storage = true;
            if (removed_count > 0U) {
                ESP_LOGW(
                    kTag,
                    "Removed %u stored sensor(s) of retired types",
                    static_cast<unsigned>(removed_count));
                // The cleaned list is already in use; a failed save is retried
                // by the same cleanup on the next boot.
                if (const esp_err_t save_err = saveInternal(handle, out_config);
                    save_err != ESP_OK) {
                    ESP_LOGW(
                        kTag,
                        "Failed to save sensor config after removing retired sensors: %s",
                        esp_err_to_name(save_err));
                }
            }
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
