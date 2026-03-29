#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "air360/sensors/sensor_driver.hpp"

namespace air360 {

class I2cBusManager;

class Ens160Sensor final : public SensorDriver {
  public:
    SensorType type() const override;
    esp_err_t init(
        const SensorRecord& record,
        const SensorDriverContext& context) override;
    esp_err_t poll() override;
    SensorMeasurement latestMeasurement() const override;
    std::string lastError() const override;

  private:
    esp_err_t setOperatingMode(std::uint8_t mode);
    esp_err_t readPartId(std::uint16_t& out_part_id);
    esp_err_t readStatus(std::uint8_t& out_status);
    esp_err_t readMetrics(std::uint8_t& out_aqi, std::uint16_t& out_tvoc, std::uint16_t& out_eco2);
    esp_err_t probeAndBindAddress();
    void setError(const std::string& message);

    SensorRecord record_{};
    I2cBusManager* i2c_bus_manager_ = nullptr;
    SensorMeasurement measurement_{};
    std::string last_error_;
    std::uint8_t active_address_ = 0U;
    bool initialized_ = false;
};

std::unique_ptr<SensorDriver> createEns160Sensor();

}  // namespace air360
