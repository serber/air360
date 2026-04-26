#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "air360/sensors/sensor_driver.hpp"

namespace air360 {

struct Bme280DriverState;

class Bme280Sensor final : public SensorDriver {
  public:
    Bme280Sensor();
    ~Bme280Sensor() override;
    SensorType type() const override;
    esp_err_t init(
        const SensorRecord& record,
        const SensorDriverContext& context) override;
    esp_err_t poll() override;
    SensorMeasurement latestMeasurement() const override;
    std::string lastError() const override;

  private:
    esp_err_t configureSensor();
    void destroyState();
    void setError(const std::string& message);

    SensorMeasurement measurement_{};
    std::string last_error_;
    std::unique_ptr<Bme280DriverState> state_;
    bool initialized_ = false;
};

std::unique_ptr<SensorDriver> createBme280Sensor();

}  // namespace air360
