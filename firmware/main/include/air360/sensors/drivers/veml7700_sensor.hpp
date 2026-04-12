#pragma once

#include <memory>
#include <string>

#include "air360/sensors/sensor_driver.hpp"
#include "veml7700.h"

namespace air360 {

class Veml7700Sensor final : public SensorDriver {
  public:
    Veml7700Sensor() = default;
    ~Veml7700Sensor() override;

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

    SensorRecord record_{};
    i2c_dev_t device_{};
    veml7700_config_t config_{};
    bool descriptor_initialized_ = false;
    SensorMeasurement measurement_{};
    std::string last_error_;
    bool initialized_ = false;
};

std::unique_ptr<SensorDriver> createVeml7700Sensor();

}  // namespace air360
