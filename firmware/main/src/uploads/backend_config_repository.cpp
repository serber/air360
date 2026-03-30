#include "air360/uploads/backend_config_repository.hpp"

#include <cstddef>
#include <cstring>
#include <string>

#include "air360/uploads/backend_registry.hpp"
#include "esp_log.h"
#include "nvs.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.backend_cfg";
constexpr char kNamespace[] = "air360";
constexpr char kConfigKey[] = "backend_cfg";

void copyString(char* destination, std::size_t destination_size, const char* source) {
    if (destination_size == 0U) {
        return;
    }

    std::strncpy(destination, source != nullptr ? source : "", destination_size - 1U);
    destination[destination_size - 1U] = '\0';
}

esp_err_t saveInternal(nvs_handle_t handle, const BackendConfigList& config) {
    esp_err_t err = nvs_set_blob(handle, kConfigKey, &config, sizeof(config));
    if (err != ESP_OK) {
        return err;
    }

    return nvs_commit(handle);
}

BackendRecord makeDefaultRecord(
    std::uint32_t id,
    BackendType type,
    const char* display_name,
    const char* endpoint_url) {
    BackendRecord record{};
    record.id = id;
    record.enabled = 0U;
    record.backend_type = type;
    copyString(record.display_name, sizeof(record.display_name), display_name);
    copyString(record.endpoint_url, sizeof(record.endpoint_url), endpoint_url);
    return record;
}

void assignDefaultBackendConfigList(BackendConfigList& config) {
    config = BackendConfigList{};
    config.backend_count = 2U;
    config.next_backend_id = 3U;
    config.upload_interval_ms = kDefaultUploadIntervalMs;
    config.backends[0] = makeDefaultRecord(
        1U,
        BackendType::kSensorCommunity,
        "Sensor.Community",
        "http://api.sensor.community/v1/push-sensor-data/");
    config.backends[1] = makeDefaultRecord(
        2U,
        BackendType::kAir360Api,
        "Air360 API",
        "");
}

}  // namespace

BackendConfigList makeDefaultBackendConfigList() {
    BackendConfigList config{};
    assignDefaultBackendConfigList(config);
    return config;
}

bool BackendConfigRepository::isValid(const BackendConfigList& config) const {
    if (config.magic != kBackendConfigMagic ||
        config.schema_version != kBackendConfigSchemaVersion ||
        config.record_size != static_cast<std::uint16_t>(sizeof(BackendRecord)) ||
        config.backend_count > kMaxConfiguredBackends ||
        config.next_backend_id == 0U ||
        config.upload_interval_ms < 1000U ||
        config.upload_interval_ms > 3600000U) {
        return false;
    }

    BackendRegistry registry;
    for (std::size_t index = 0; index < config.backend_count; ++index) {
        std::string error;
        if (!registry.validateRecord(config.backends[index], error)) {
            return false;
        }
    }

    return true;
}

esp_err_t BackendConfigRepository::loadOrCreate(
    BackendConfigList& out_config,
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
        assignDefaultBackendConfigList(out_config);
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

    if (blob_size == sizeof(BackendConfigList)) {
        out_config = BackendConfigList{};
        err = nvs_get_blob(handle, kConfigKey, &out_config, &blob_size);
        if (err == ESP_OK &&
            blob_size == sizeof(BackendConfigList) &&
            isValid(out_config)) {
            loaded_from_storage = true;
            nvs_close(handle);
            return ESP_OK;
        }
    }

    ESP_LOGW(kTag, "Stored backend config invalid or incompatible, replacing with defaults");
    assignDefaultBackendConfigList(out_config);
    err = saveInternal(handle, out_config);
    if (err == ESP_OK) {
        wrote_defaults = true;
    }
    nvs_close(handle);
    return err;
}

esp_err_t BackendConfigRepository::save(const BackendConfigList& config) {
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
