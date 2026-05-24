#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "air360/sensors/sensor_driver.hpp"
#include "freertos/FreeRTOS.h"

namespace air360 {

class Pmsx003Sensor final : public SensorDriver {
  public:
    Pmsx003Sensor() = default;
    ~Pmsx003Sensor() override;

    SensorType type() const override;
    esp_err_t init(
        const SensorRecord& record,
        const SensorDriverContext& context) override;
    esp_err_t poll() override;
    SensorMeasurement latestMeasurement() const override;

  private:
    esp_err_t drainUartEvents();
    esp_err_t readAvailableFrames(bool& out_found_frame);
    void feedByte(std::uint8_t byte, bool& out_frame_ready);
    esp_err_t decodeFrame();
    void storeMeasurement();
    esp_err_t handlePollError(esp_err_t err, const char* message);
    void resetParser();

    SensorRecord record_{};
    UartPortManager* uart_port_manager_ = nullptr;
    SensorMeasurement measurement_{};
    std::array<std::uint8_t, 32U> frame_{};
    std::size_t frame_index_ = 0U;
    std::uint32_t uart_overrun_count_ = 0U;
};

std::unique_ptr<SensorDriver> createPmsx003Sensor();

}  // namespace air360
