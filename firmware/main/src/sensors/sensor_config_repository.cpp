#include "air360/sensors/sensor_config_repository.hpp"

#include <cstddef>
#include <cstring>
#include <string>

#include "air360/sensors/sensor_registry.hpp"
#include "esp_log.h"
#include "nvs.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.sensor_cfg";
constexpr char kNamespace[] = "air360";
constexpr char kConfigKey[] = "sensor_cfg";

constexpr std::uint16_t kSensorConfigSchemaVersionV1 = 1U;

struct SensorRecordV1 {
    std::uint32_t id = 0U;
    std::uint8_t enabled = 1U;
    SensorType sensor_type = SensorType::kUnknown;
    TransportKind transport_kind = TransportKind::kUnknown;
    std::uint32_t poll_interval_ms = 10000U;
    char display_name[kSensorDisplayNameCapacity]{};
    std::uint8_t i2c_bus_id = 0U;
    std::uint8_t i2c_address = 0x76U;
    std::int16_t analog_gpio_pin = -1;
    std::uint8_t reserved0[8]{};
};

struct SensorConfigListV1 {
    std::uint32_t magic = kSensorConfigMagic;
    std::uint16_t schema_version = kSensorConfigSchemaVersionV1;
    std::uint16_t record_size = static_cast<std::uint16_t>(sizeof(SensorRecordV1));
    std::uint16_t sensor_count = 0U;
    std::uint16_t reserved0 = 0U;
    std::uint32_t next_sensor_id = 1U;
    std::array<SensorRecordV1, kMaxConfiguredSensors> sensors{};
};

esp_err_t saveInternal(nvs_handle_t handle, const SensorConfigList& config) {
    esp_err_t err = nvs_set_blob(handle, kConfigKey, &config, sizeof(config));
    if (err != ESP_OK) {
        return err;
    }

    return nvs_commit(handle);
}

SensorConfigList migrateV1ToV2(const SensorConfigListV1& config_v1) {
    SensorConfigList migrated = makeDefaultSensorConfigList();
    migrated.sensor_count = config_v1.sensor_count;
    migrated.next_sensor_id = config_v1.next_sensor_id;

    for (std::size_t index = 0; index < config_v1.sensor_count; ++index) {
        const SensorRecordV1& old_record = config_v1.sensors[index];
        SensorRecord& new_record = migrated.sensors[index];
        new_record.id = old_record.id;
        new_record.enabled = old_record.enabled;
        new_record.sensor_type = old_record.sensor_type;
        new_record.transport_kind = old_record.transport_kind;
        new_record.poll_interval_ms = old_record.poll_interval_ms;
        std::memcpy(new_record.display_name, old_record.display_name, sizeof(new_record.display_name));
        new_record.i2c_bus_id = old_record.i2c_bus_id;
        new_record.i2c_address = old_record.i2c_address;
        new_record.analog_gpio_pin = old_record.analog_gpio_pin;
        new_record.uart_port_id = 1U;
        new_record.uart_rx_gpio_pin = -1;
        new_record.uart_tx_gpio_pin = -1;
        new_record.uart_baud_rate = 9600U;
    }

    return migrated;
}

}  // namespace

bool SensorConfigRepository::isValid(const SensorConfigList& config) const {
    if (config.magic != kSensorConfigMagic ||
        config.schema_version != kSensorConfigSchemaVersion ||
        config.record_size != static_cast<std::uint16_t>(sizeof(SensorRecord)) ||
        config.sensor_count > kMaxConfiguredSensors ||
        config.next_sensor_id == 0U) {
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

    if (blob_size == sizeof(SensorConfigListV1)) {
        SensorConfigListV1 loaded_v1{};
        err = nvs_get_blob(handle, kConfigKey, &loaded_v1, &blob_size);
        if (err != ESP_OK) {
            nvs_close(handle);
            return err;
        }

        if (loaded_v1.magic == kSensorConfigMagic &&
            loaded_v1.schema_version == kSensorConfigSchemaVersionV1 &&
            loaded_v1.record_size == static_cast<std::uint16_t>(sizeof(SensorRecordV1)) &&
            loaded_v1.sensor_count <= kMaxConfiguredSensors &&
            loaded_v1.next_sensor_id != 0U) {
            ESP_LOGI(kTag, "Migrating stored sensor config from schema v1 to v2");
            out_config = migrateV1ToV2(loaded_v1);
            err = saveInternal(handle, out_config);
            if (err == ESP_OK) {
                loaded_from_storage = true;
            }
            nvs_close(handle);
            return err;
        }
    } else if (blob_size == sizeof(SensorConfigList)) {
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
