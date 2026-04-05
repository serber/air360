#pragma once

#include <array>
#include <cstdint>

#include "air360/sensors/sensor_types.hpp"

namespace air360 {

constexpr std::uint32_t kSensorConfigMagic = 0x41333631U;
constexpr std::uint16_t kSensorConfigSchemaVersion = 3U;

struct SensorRecord {
    std::uint32_t id = 0U;
    std::uint8_t enabled = 1U;
    SensorType sensor_type = SensorType::kUnknown;
    TransportKind transport_kind = TransportKind::kUnknown;
    std::uint32_t poll_interval_ms = 10000U;
    std::uint8_t i2c_bus_id = 0U;
    std::uint8_t i2c_address = 0x77U;
    std::uint8_t uart_port_id = 1U;
    std::uint8_t reserved0 = 0U;
    std::int16_t analog_gpio_pin = -1;
    std::int16_t uart_rx_gpio_pin = -1;
    std::int16_t uart_tx_gpio_pin = -1;
    std::uint32_t uart_baud_rate = 9600U;
    std::uint8_t reserved1[12]{};
};

struct SensorConfigList {
    std::uint32_t magic = kSensorConfigMagic;
    std::uint16_t schema_version = kSensorConfigSchemaVersion;
    std::uint16_t record_size = static_cast<std::uint16_t>(sizeof(SensorRecord));
    std::uint16_t sensor_count = 0U;
    std::uint16_t reserved0 = 0U;
    std::uint32_t next_sensor_id = 1U;
    std::array<SensorRecord, kMaxConfiguredSensors> sensors{};
};

inline SensorConfigList makeDefaultSensorConfigList() {
    return SensorConfigList{};
}

inline SensorRecord* findSensorRecordById(SensorConfigList& config, std::uint32_t id) {
    for (std::size_t index = 0; index < config.sensor_count; ++index) {
        if (config.sensors[index].id == id) {
            return &config.sensors[index];
        }
    }
    return nullptr;
}

inline const SensorRecord* findSensorRecordById(
    const SensorConfigList& config,
    std::uint32_t id) {
    for (std::size_t index = 0; index < config.sensor_count; ++index) {
        if (config.sensors[index].id == id) {
            return &config.sensors[index];
        }
    }
    return nullptr;
}

inline bool eraseSensorRecordById(SensorConfigList& config, std::uint32_t id) {
    for (std::size_t index = 0; index < config.sensor_count; ++index) {
        if (config.sensors[index].id != id) {
            continue;
        }

        for (std::size_t move_index = index + 1U; move_index < config.sensor_count; ++move_index) {
            config.sensors[move_index - 1U] = config.sensors[move_index];
        }

        if (config.sensor_count > 0U) {
            --config.sensor_count;
            config.sensors[config.sensor_count] = SensorRecord{};
        }
        return true;
    }

    return false;
}

}  // namespace air360
