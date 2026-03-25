#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "air360/sensors/sensor_driver.hpp"

namespace air360 {

class Bme280Sensor final : public SensorDriver {
  public:
    SensorType type() const override;
    esp_err_t init(
        const SensorRecord& record,
        const SensorDriverContext& context) override;
    esp_err_t poll() override;
    SensorMeasurement latestMeasurement() const override;
    std::string lastError() const override;

  private:
    struct CalibrationData {
        std::uint16_t dig_t1 = 0U;
        std::int16_t dig_t2 = 0;
        std::int16_t dig_t3 = 0;
        std::uint16_t dig_p1 = 0U;
        std::int16_t dig_p2 = 0;
        std::int16_t dig_p3 = 0;
        std::int16_t dig_p4 = 0;
        std::int16_t dig_p5 = 0;
        std::int16_t dig_p6 = 0;
        std::int16_t dig_p7 = 0;
        std::int16_t dig_p8 = 0;
        std::int16_t dig_p9 = 0;
        std::uint8_t dig_h1 = 0U;
        std::int16_t dig_h2 = 0;
        std::uint8_t dig_h3 = 0U;
        std::int16_t dig_h4 = 0;
        std::int16_t dig_h5 = 0;
        std::int8_t dig_h6 = 0;
    };

    esp_err_t resetSensor();
    esp_err_t readChipId(std::uint8_t& chip_id);
    esp_err_t readChipIdWithRetry(std::uint8_t& chip_id);
    esp_err_t readCalibration();
    esp_err_t configureSensor();
    esp_err_t startForcedMeasurement();
    esp_err_t waitForMeasurement();
    esp_err_t readRawValues(
        std::int32_t& raw_temperature,
        std::int32_t& raw_pressure,
        std::int32_t& raw_humidity);
    void setError(const std::string& message);

    SensorRecord record_{};
    I2cBusManager* i2c_bus_manager_ = nullptr;
    CalibrationData calibration_{};
    SensorMeasurement measurement_{};
    std::int32_t t_fine_ = 0;
    std::string last_error_;
    bool initialized_ = false;
};

std::unique_ptr<SensorDriver> createBme280Sensor();

}  // namespace air360
