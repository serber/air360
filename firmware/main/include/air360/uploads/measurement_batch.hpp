#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "air360/network_manager.hpp"
#include "air360/sensors/sensor_driver.hpp"
#include "air360/sensors/sensor_types.hpp"

namespace air360 {

struct MeasurementSample {
    std::uint32_t sensor_id = 0U;
    SensorType sensor_type = SensorType::kUnknown;
    std::uint64_t sample_time_ms = 0U;
    SensorMeasurement measurement{};
};

struct MeasurementPoint {
    std::uint32_t sensor_id = 0U;
    SensorType sensor_type = SensorType::kUnknown;
    SensorValueKind value_kind = SensorValueKind::kUnknown;
    float value = 0.0F;
    std::uint64_t sample_time_ms = 0U;
};

struct MeasurementBatch {
    std::uint64_t batch_id = 0U;
    std::uint64_t created_uptime_ms = 0U;
    std::int64_t created_unix_ms = 0;
    std::string device_name;
    std::string project_version;
    std::string chip_id;
    std::string short_chip_id;
    std::string esp_mac_id;
    NetworkMode network_mode = NetworkMode::kOffline;
    bool station_connected = false;
    std::vector<MeasurementPoint> points;

    bool empty() const {
        return points.empty();
    }
};

}  // namespace air360
