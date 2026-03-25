#pragma once

#include <string>

#include "air360/sensors/sensor_config.hpp"
#include "esp_err.h"

namespace air360 {

class SensorDriver {
  public:
    virtual ~SensorDriver() = default;

    virtual SensorType type() const = 0;
    virtual esp_err_t init(const SensorRecord& record) = 0;
    virtual esp_err_t poll() = 0;
    virtual std::string lastError() const = 0;
};

}  // namespace air360
