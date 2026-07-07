#pragma once

#include <memory>

#include "air360/sensors/sensor_driver.hpp"
#include "bmp280.h"

namespace air360 {

class Bme280Sensor final : public SensorDriver {
  public:
    Bme280Sensor() = default;
    ~Bme280Sensor() override;

    SensorType type() const override;
    esp_err_t init(
        const SensorRecord& record,
        const SensorDriverContext& context) override;
    esp_err_t poll() override;
    SensorMeasurement latestMeasurement() const override;

  private:
    void teardown();

    bmp280_t device_{};
    bool descriptor_initialized_ = false;
    SensorMeasurement measurement_{};
};

std::unique_ptr<SensorDriver> createBme280Sensor();

}  // namespace air360
