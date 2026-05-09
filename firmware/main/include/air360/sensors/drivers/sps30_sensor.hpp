#pragma once

#include <memory>

#include "air360/sensors/sensor_driver.hpp"
#include "i2cdev.h"

namespace air360 {

class Sps30Sensor final : public SensorDriver {
  public:
    ~Sps30Sensor();
    SensorType type() const override;
    esp_err_t init(
        const SensorRecord& record,
        const SensorDriverContext& context) override;
    esp_err_t poll() override;
    SensorMeasurement latestMeasurement() const override;

  private:
    esp_err_t startMeasurement();
    void teardown();

    SensorRecord record_{};
    i2c_dev_t device_{};
    bool device_initialized_ = false;
    SensorMeasurement measurement_{};
};

std::unique_ptr<SensorDriver> createSps30Sensor();

}  // namespace air360
