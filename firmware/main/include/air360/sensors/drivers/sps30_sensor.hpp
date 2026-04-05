#pragma once

#include <memory>
#include <string>

#include "air360/sensors/sensor_driver.hpp"

namespace air360 {

class Sps30Sensor final : public SensorDriver {
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
    esp_err_t startMeasurement();

    SensorRecord record_{};
    I2cBusManager* i2c_bus_manager_ = nullptr;
    SensorMeasurement measurement_{};
    std::string last_error_;
    bool initialized_ = false;
};

std::unique_ptr<SensorDriver> createSps30Sensor();

}  // namespace air360
