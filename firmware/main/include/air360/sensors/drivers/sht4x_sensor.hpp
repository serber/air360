#pragma once

#include <memory>
#include <string>

#include "air360/sensors/sensor_driver.hpp"
#include "sht4x.h"

namespace air360 {

class Sht4xSensor final : public SensorDriver {
  public:
    Sht4xSensor() = default;
    ~Sht4xSensor() override;

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
    sht4x_t device_{};
    bool descriptor_initialized_ = false;
    SensorMeasurement measurement_{};
    std::string last_error_;
    bool initialized_ = false;
};

std::unique_ptr<SensorDriver> createSht4xSensor();

}  // namespace air360
