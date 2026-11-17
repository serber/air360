#include "air360/uploads/backend_config_repository.hpp"

#include <string>

#include "air360/string_utils.hpp"
#include "air360/uploads/backend_registry.hpp"
#include "esp_log.h"
#include "nvs.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.backend_cfg";
constexpr char kNamespace[] = "air360";
constexpr char kConfigKey[] = "backend_cfg";

[[nodiscard]] esp_err_t saveInternal(nvs_handle_t handle, const BackendConfigList& config) {
    esp_err_t err = nvs_set_blob(handle, kConfigKey, &config, sizeof(config));
    if (err != ESP_OK) {
        return err;
    }

    return nvs_commit(handle);
}

BackendRecord makeDefaultRecord(std::uint32_t id, const BackendDescriptor& descriptor) {
    BackendRecord record{};
    record.id = id;
    record.enabled = 0U;
    record.backend_type = descriptor.type;
    copyString(record.display_name, sizeof(record.display_name), descriptor.display_name);
    copyString(record.host, sizeof(record.host), descriptor.defaults.host);
    copyString(record.path, sizeof(record.path), descriptor.defaults.path);
    record.port = descriptor.defaults.port;
    record.protocol = descriptor.defaults.protocol;
    if (descriptor.type == BackendType::kInfluxDb) {
        copyString(record.influxdb_measurement, sizeof(record.influxdb_measurement), "air360");
    }
    return record;
}

bool appendDefaultRecordForDescriptor(
    BackendConfigList& config,
    const BackendDescriptor& descriptor) {
    if (config.backend_count >= kMaxConfiguredBackends) {
        return false;
    }

    const std::uint32_t id = config.next_backend_id++;
    config.backends[config.backend_count++] = makeDefaultRecord(id, descriptor);
    return true;
}

bool normalizeBackendConfigList(BackendConfigList& config) {
    BackendRegistry registry;
    bool changed = false;

    for (std::size_t index = 0; index < registry.descriptorCount(); ++index) {
        const BackendDescriptor& descriptor = registry.descriptors()[index];
        if (findBackendRecordByType(config, descriptor.type) != nullptr) {
            continue;
        }

        if (!appendDefaultRecordForDescriptor(config, descriptor)) {
            break;
        }
        changed = true;
    }

    return changed;
}

// A blob written before location validation existed may hold nan/inf or
// out-of-range coordinates. Reset just that location to "not set" instead of
// discarding the whole backend config as invalid.
bool sanitizeAir360Location(BackendConfigList& config) {
    bool changed = false;
    const std::size_t count =
        config.backend_count < config.backends.size() ? config.backend_count : config.backends.size();
    for (std::size_t index = 0; index < count; ++index) {
        BackendRecord& record = config.backends[index];
        if (record.backend_type == BackendType::kAir360Api && !hasValidAir360Location(record)) {
            record.latitude = 0.0F;
            record.longitude = 0.0F;
            record.altitude_m = 0.0F;
            changed = true;
        }
    }
    return changed;
}

void assignDefaultBackendConfigList(BackendConfigList& config) {
    config = BackendConfigList{};
    config.upload_interval_ms = kDefaultUploadIntervalMs;
    normalizeBackendConfigList(config);
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
        config.upload_interval_ms < kMinUploadIntervalMs ||
        config.upload_interval_ms > kMaxUploadIntervalMs) {
        return false;
    }

    BackendRegistry registry;
    for (std::size_t index = 0; index < config.backend_count; ++index) {
        std::string error;
        if (!registry.validateRecord(config.backends[index], error)) {
            return false;
        }
        // Checked regardless of enabled: a disabled record keeps its location
        // and must not carry non-finite values into a later registration.
        if (config.backends[index].backend_type == BackendType::kAir360Api &&
            !hasValidAir360Location(config.backends[index])) {
            return false;
        }

        for (std::size_t compare = index + 1U; compare < config.backend_count; ++compare) {
            if (config.backends[index].backend_type == config.backends[compare].backend_type) {
                return false;
            }
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
        const bool location_sanitized =
            err == ESP_OK && sanitizeAir360Location(out_config);
        if (location_sanitized) {
            ESP_LOGW(kTag, "Stored Air360 location was not finite or out of range; cleared it");
        }
        if (err == ESP_OK &&
            blob_size == sizeof(BackendConfigList) &&
            isValid(out_config)) {
            if (normalizeBackendConfigList(out_config) || location_sanitized) {
                err = saveInternal(handle, out_config);
                if (err != ESP_OK) {
                    nvs_close(handle);
                    return err;
                }
            }
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
    BackendConfigList normalized = config;
    normalizeBackendConfigList(normalized);

    if (!isValid(normalized)) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = saveInternal(handle, normalized);
    nvs_close(handle);
    return err;
}

}  // namespace air360
