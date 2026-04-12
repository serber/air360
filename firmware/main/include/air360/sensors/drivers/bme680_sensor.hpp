#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "air360/sensors/sensor_driver.hpp"

namespace air360 {

struct Bme680DriverState;

class Bme680Sensor final : public SensorDriver {
  public:
    ~Bme680Sensor() override;
    SensorType type() const override;
    esp_err_t init(
        const SensorRecord& record,
        const SensorDriverContext& context) override;
    esp_err_t poll() override;
    SensorMeasurement latestMeasurement() const override;
    std::string lastError() const override;

  private:
    esp_err_t configureSensor();
    void reset();
    void setError(const std::string& message);

    SensorRecord record_{};
    SensorMeasurement measurement_{};
    std::string last_error_;
    Bme680DriverState* state_ = nullptr;
    bool initialized_ = false;
};

std::unique_ptr<SensorDriver> createBme680Sensor();

}  // namespace air360
