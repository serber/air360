#pragma once

#include <memory>

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

  private:
    void teardown();

    SensorRecord record_{};
    aht30_handle_t handle_ = nullptr;
    SensorMeasurement measurement_{};
};

std::unique_ptr<SensorDriver> createAht30Sensor();

}  // namespace air360
