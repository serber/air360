#pragma once

#include <memory>

#include "air360/sensors/sensor_driver.hpp"
#include "bme280.h"
#include "i2c_bus.h"
#include "i2cdev.h"

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
    esp_err_t configureSensor();
    void teardown();

    SensorMeasurement measurement_{};
    i2c_dev_t dev_{};
    bool dev_initialized_ = false;
    i2c_bus_handle_t bus_ = nullptr;
    bme280_handle_t sensor_ = nullptr;
};

std::unique_ptr<SensorDriver> createBme280Sensor();

}  // namespace air360
