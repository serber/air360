#pragma once

#include <cstdint>
#include <string>

#include "air360/sensors/sensor_config.hpp"
#include "esp_err.h"

namespace air360 {

class I2cBusManager;

struct SensorMeasurement {
    bool has_temperature = false;
    bool has_humidity = false;
    bool has_pressure = false;
    float temperature_c = 0.0F;
    float humidity_percent = 0.0F;
    float pressure_hpa = 0.0F;
    std::uint64_t sample_time_ms = 0U;
};

struct SensorDriverContext {
    I2cBusManager* i2c_bus_manager = nullptr;
};

class SensorDriver {
  public:
    virtual ~SensorDriver() = default;

    virtual SensorType type() const = 0;
    virtual esp_err_t init(
        const SensorRecord& record,
        const SensorDriverContext& context) = 0;
    virtual esp_err_t poll() = 0;
    virtual SensorMeasurement latestMeasurement() const = 0;
    virtual std::string lastError() const = 0;
};

}  // namespace air360
