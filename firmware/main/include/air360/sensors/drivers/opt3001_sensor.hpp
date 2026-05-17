#pragma once

#include <cstdint>
#include <memory>

#include "air360/sensors/sensor_driver.hpp"
#include "i2cdev.h"

namespace air360 {

class Opt3001Sensor final : public SensorDriver {
  public:
    Opt3001Sensor() = default;
    ~Opt3001Sensor() override;

    SensorType type() const override;
    esp_err_t init(
        const SensorRecord& record,
        const SensorDriverContext& context) override;
    esp_err_t poll() override;
    SensorMeasurement latestMeasurement() const override;

  private:
    esp_err_t readRegister(std::uint8_t reg, std::uint16_t& out_value);
    esp_err_t writeRegister(std::uint8_t reg, std::uint16_t value);
    esp_err_t probe();
    void teardown();

    i2c_dev_t dev_{};
    bool dev_initialized_ = false;
    SensorMeasurement measurement_{};
};

std::unique_ptr<SensorDriver> createOpt3001Sensor();

}  // namespace air360
