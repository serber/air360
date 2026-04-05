#pragma once

#include <memory>
#include <string>

#include "air360/sensors/sensor_driver.hpp"

class TinyGPSPlus;

namespace air360 {

class GpsNmeaSensor final : public SensorDriver {
  public:
    ~GpsNmeaSensor() override;

    SensorType type() const override;
    esp_err_t init(
        const SensorRecord& record,
        const SensorDriverContext& context) override;
    esp_err_t poll() override;
    SensorMeasurement latestMeasurement() const override;
    std::string lastError() const override;

  private:
    void rebuildMeasurement();
    void setError(const std::string& message);

    SensorRecord record_{};
    UartPortManager* uart_port_manager_ = nullptr;
    std::unique_ptr<TinyGPSPlus> parser_;
    SensorMeasurement measurement_{};
    std::string last_error_;
    bool initialized_ = false;
};

std::unique_ptr<SensorDriver> createGpsNmeaSensor();

}  // namespace air360
