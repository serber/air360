#pragma once

#include <cstdint>
#include <memory>

#include "air360/sensors/sensor_driver.hpp"
#include "i2cdev.h"

namespace air360 {

// LMP91000 AFE board: an LMP91000 potentiostat (configured over I2C) drives a
// 3-electrode electrochemical cell (Winsen ME3 series), and an ADS1115 on the
// same bus digitises the LMP91000 VOUT (AIN0) and the external 2.5 V
// reference (AIN1). The record's I2C address selects the LMP91000; the
// ADS1115 address is fixed by the board wiring (ADDR -> VDD, 0x49).
class Lmp91000Sensor final : public SensorDriver {
  public:
    Lmp91000Sensor() = default;
    ~Lmp91000Sensor() override;

    SensorType type() const override;
    esp_err_t init(
        const SensorRecord& record,
        const SensorDriverContext& context) override;
    esp_err_t poll() override;
    SensorMeasurement latestMeasurement() const override;

  private:
    esp_err_t waitLmpReady();
    esp_err_t configureLmp();
    esp_err_t writeLmpRegister(std::uint8_t reg, std::uint8_t value);
    esp_err_t readLmpRegister(std::uint8_t reg, std::uint8_t& out_value);
    esp_err_t readAdsChannelVolts(std::uint16_t config, float& out_volts);
    void teardown();

    i2c_dev_t lmp_dev_{};
    i2c_dev_t ads_dev_{};
    bool lmp_dev_initialized_ = false;
    bool ads_dev_initialized_ = false;
    std::uint64_t configured_at_ms_ = 0U;
    SensorMeasurement measurement_{};
};

std::unique_ptr<SensorDriver> createLmp91000Sensor();

}  // namespace air360
