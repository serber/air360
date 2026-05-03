#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "air360/sensors/sensor_driver.hpp"
#include "aht30.h"

namespace air360 {

class Aht30Sensor final : public SensorDriver {
  public:
    Aht30Sensor() = default;
    ~Aht30Sensor() override;

    SensorType type() const override;
    esp_err_t init(
        const SensorRecord& record,
        const SensorDriverContext& context) override;
    esp_err_t poll() override;
    SensorMeasurement latestMeasurement() const override;
    std::string lastError() const override;

  private:
    void reset();
    void setError(const std::string& message);

    SensorRecord record_{};
    aht30_handle_t handle_ = nullptr;
    SensorMeasurement measurement_{};
    std::string last_error_;
    bool initialized_ = false;
};

std::unique_ptr<SensorDriver> createAht30Sensor();

}  // namespace air360
