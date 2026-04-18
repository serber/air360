#pragma once

#include <memory>
#include <string>

#include "air360/sensors/sensor_driver.hpp"
#include "ina219.h"

namespace air360 {

class Ina219Sensor final : public SensorDriver {
  public:
    Ina219Sensor() = default;
    ~Ina219Sensor() override;

    SensorType type() const override;
    esp_err_t init(
        const SensorRecord& record,
        const SensorDriverContext& context) override;
    esp_err_t poll() override;
    SensorMeasurement latestMeasurement() const override;
    std::string lastError() const override;

  private:
    void reset();
    void setError(const std::string& message);

    ina219_t device_{};
    bool descriptor_initialized_ = false;
    SensorMeasurement measurement_{};
    std::string last_error_;
    bool initialized_ = false;
};

std::unique_ptr<SensorDriver> createIna219Sensor();

}  // namespace air360
