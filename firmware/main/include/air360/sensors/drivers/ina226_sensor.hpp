#pragma once

#include <memory>

#include "air360/sensors/sensor_driver.hpp"
#include "ina226.h"

namespace air360 {

class Ina226Sensor final : public SensorDriver {
  public:
    Ina226Sensor() = default;
    ~Ina226Sensor() override;

    SensorType type() const override;
    esp_err_t init(
        const SensorRecord& record,
        const SensorDriverContext& context) override;
    esp_err_t poll() override;
    SensorMeasurement latestMeasurement() const override;

  private:
    void teardown();

    ina226_handle_t handle_ = nullptr;
    SensorMeasurement measurement_{};
};

std::unique_ptr<SensorDriver> createIna226Sensor();

}  // namespace air360
