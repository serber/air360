#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <string>

#include "air360/sensors/sensor_driver.hpp"

namespace air360 {

class Sds011Sensor final : public SensorDriver {
  public:
    ~Sds011Sensor() override = default;

    SensorType type() const override;
    esp_err_t init(
        const SensorRecord& record,
        const SensorDriverContext& context) override;
    esp_err_t poll() override;
    SensorMeasurement latestMeasurement() const override;
    std::string lastError() const override;

  private:
    bool consumeByte(std::uint8_t byte);
    void setError(const std::string& message);

    SensorRecord record_{};
    UartPortManager* uart_port_manager_ = nullptr;
    SensorMeasurement measurement_{};
    std::string last_error_;
    std::array<std::uint8_t, 10> frame_{};
    std::size_t frame_size_ = 0U;
    bool initialized_ = false;
};

std::unique_ptr<SensorDriver> createSds011Sensor();

}  // namespace air360
