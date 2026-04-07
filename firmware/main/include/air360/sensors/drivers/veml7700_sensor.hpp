#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "air360/sensors/sensor_driver.hpp"

class Adafruit_VEML7700;
class TwoWire;

namespace air360 {

class I2cBusManager;

class Veml7700Sensor final : public SensorDriver {
  public:
    SensorType type() const override;
    esp_err_t init(
        const SensorRecord& record,
        const SensorDriverContext& context) override;
    esp_err_t poll() override;
    SensorMeasurement latestMeasurement() const override;
    std::string lastError() const override;

  private:
    void setError(const std::string& message);

    SensorRecord record_{};
    I2cBusManager* i2c_bus_manager_ = nullptr;
    SensorMeasurement measurement_{};
    std::string last_error_;
    std::unique_ptr<::TwoWire> wire_;
    std::unique_ptr<::Adafruit_VEML7700> sensor_;
    bool initialized_ = false;
};

std::unique_ptr<SensorDriver> createVeml7700Sensor();

}  // namespace air360
