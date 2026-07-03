#pragma once

#include <memory>

#include "air360/sensors/sensor_driver.hpp"
#include "bmp390.h"

namespace air360 {

class Bmp390Sensor final : public SensorDriver {
  public:
    Bmp390Sensor() = default;
    ~Bmp390Sensor() override;

    SensorType type() const override;
    esp_err_t init(
        const SensorRecord& record,
        const SensorDriverContext& context) override;
    esp_err_t poll() override;
    SensorMeasurement latestMeasurement() const override;

  private:
    void teardown();

    SensorRecord record_{};
    bmp390_handle_t handle_ = nullptr;
    SensorMeasurement measurement_{};
};

std::unique_ptr<SensorDriver> createBmp390Sensor();

}  // namespace air360
