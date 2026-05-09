#pragma once

#include <memory>

#include "air360/sensors/sensor_driver.hpp"
#include "i2cdev.h"

namespace air360 {

class Htu2xSensor final : public SensorDriver {
  public:
    Htu2xSensor() = default;
    ~Htu2xSensor() override;

    SensorType type() const override;
    esp_err_t init(
        const SensorRecord& record,
        const SensorDriverContext& context) override;
    esp_err_t poll() override;
    SensorMeasurement latestMeasurement() const override;

  private:
    void teardown();

    SensorRecord record_{};
    i2c_dev_t device_{};
    bool descriptor_initialized_ = false;
    SensorMeasurement measurement_{};
};

std::unique_ptr<SensorDriver> createHtu2xSensor();

}  // namespace air360
