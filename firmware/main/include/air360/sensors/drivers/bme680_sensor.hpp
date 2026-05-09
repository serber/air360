#pragma once

#include <memory>

#include "air360/sensors/sensor_driver.hpp"
#include "bme680.h"

namespace air360 {

class Bme680Sensor final : public SensorDriver {
  public:
    Bme680Sensor() = default;
    ~Bme680Sensor() override;
    SensorType type() const override;
    esp_err_t init(
        const SensorRecord& record,
        const SensorDriverContext& context) override;
    esp_err_t poll() override;
    SensorMeasurement latestMeasurement() const override;

  private:
    esp_err_t configureSensor();
    void teardown();

    SensorRecord record_{};
    SensorMeasurement measurement_{};
    bme680_t device_{};
    bool descriptor_initialized_ = false;
};

std::unique_ptr<SensorDriver> createBme680Sensor();

}  // namespace air360
