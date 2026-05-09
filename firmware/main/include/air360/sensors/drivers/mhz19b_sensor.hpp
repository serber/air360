#pragma once

#include <memory>

#include "air360/sensors/sensor_driver.hpp"
#include "mhz19b.h"

namespace air360 {

class Mhz19bSensor final : public SensorDriver {
  public:
    Mhz19bSensor() = default;
    ~Mhz19bSensor() override;

    SensorType type() const override;
    esp_err_t init(
        const SensorRecord& record,
        const SensorDriverContext& context) override;
    esp_err_t poll() override;
    SensorMeasurement latestMeasurement() const override;

  private:
    void teardown();

    mhz19b_dev_t device_{};
    SensorRecord record_{};
    UartPortManager* uart_port_manager_ = nullptr;
    SensorMeasurement measurement_{};
};

std::unique_ptr<SensorDriver> createMhz19bSensor();

}  // namespace air360
