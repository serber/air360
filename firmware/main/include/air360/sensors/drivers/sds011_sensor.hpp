#pragma once

#include <memory>
#include <string>

#include "air360/sensors/sensor_driver.hpp"

namespace air360 {

// Driver for the Nova Fitness SDS011 laser particulate matter sensor.
// Communicates via UART at 9600 baud (8N1) in the default active reporting
// mode: the sensor streams one 10-byte binary frame per second autonomously.
// The driver reads all accumulated frames per poll cycle and reports the
// most recent valid reading.
class Sds011Sensor final : public SensorDriver {
  public:
    SensorType type() const override;
    esp_err_t init(
        const SensorRecord& record,
        const SensorDriverContext& context) override;
    esp_err_t poll() override;
    SensorMeasurement latestMeasurement() const override;
    std::string lastError() const override;

  private:
    void setError(const std::string& message);

    SensorRecord record_{};
    UartPortManager* uart_port_manager_ = nullptr;
    SensorMeasurement measurement_{};
    std::string last_error_;
    bool initialized_ = false;
};

std::unique_ptr<SensorDriver> createSds011Sensor();

}  // namespace air360
