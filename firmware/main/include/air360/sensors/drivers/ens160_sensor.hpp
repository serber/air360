#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "air360/sensors/sensor_driver.hpp"

class ScioSense_ENS160;
class TwoWire;

namespace air360 {

class I2cBusManager;

class Ens160Sensor final : public SensorDriver {
  public:
    ~Ens160Sensor() override;
    SensorType type() const override;
    esp_err_t init(
        const SensorRecord& record,
        const SensorDriverContext& context) override;
    esp_err_t poll() override;
    SensorMeasurement latestMeasurement() const override;
    std::string lastError() const override;

  private:
    esp_err_t bindSensorAtAddress(std::uint8_t address);
    esp_err_t bindConfiguredAddress();
    void setError(const std::string& message);

    SensorRecord record_{};
    I2cBusManager* i2c_bus_manager_ = nullptr;
    SensorMeasurement measurement_{};
    std::string last_error_;
    std::unique_ptr<::TwoWire> wire_;
    std::unique_ptr<::ScioSense_ENS160> sensor_;
    std::uint8_t active_address_ = 0U;
    bool initialized_ = false;
};

std::unique_ptr<SensorDriver> createEns160Sensor();

}  // namespace air360
