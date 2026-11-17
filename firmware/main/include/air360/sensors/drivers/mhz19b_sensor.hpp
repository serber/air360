#pragma once

#include <array>
#include <cstdint>
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
    // Receive buffer handed to the component instead of the heap buffer that
    // mhz19b_init() would allocate, so no teardown path can leak it.
    std::array<std::uint8_t, MHZ19B_SERIAL_BUF_LEN> rx_buffer_{};
    UartPortManager* uart_port_manager_ = nullptr;
    SensorMeasurement measurement_{};
};

std::unique_ptr<SensorDriver> createMhz19bSensor();

}  // namespace air360
