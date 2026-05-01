#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "air360/sensors/sensor_driver.hpp"
#include "freertos/FreeRTOS.h"

namespace air360 {

class Sds011Sensor final : public SensorDriver {
  public:
    Sds011Sensor() = default;
    ~Sds011Sensor() override = default;

    SensorType type() const override;
    esp_err_t init(
        const SensorRecord& record,
        const SensorDriverContext& context) override;
    esp_err_t poll() override;
    SensorMeasurement latestMeasurement() const override;
    std::string lastError() const override;

  private:
    struct Reading {
        float pm2_5_ug_m3 = 0.0F;
        float pm10_ug_m3 = 0.0F;
    };

    esp_err_t drainUartEvents();
    esp_err_t sendQueryCommand();
    esp_err_t readAvailableFrames(bool& out_found_frame);
    void feedByte(std::uint8_t byte, bool& out_frame_ready, Reading& out_reading);
    bool decodeFrame(Reading& out_reading) const;
    void storeMeasurement(const Reading& reading);
    esp_err_t handlePollError(esp_err_t err, const char* message);
    void resetParser();
    void setError(const std::string& message);

    SensorRecord record_{};
    UartPortManager* uart_port_manager_ = nullptr;
    SensorMeasurement measurement_{};
    std::string last_error_;
    std::array<std::uint8_t, 10U> frame_{};
    std::size_t frame_index_ = 0U;
    std::uint32_t uart_overrun_count_ = 0U;
    bool initialized_ = false;
};

std::unique_ptr<SensorDriver> createSds011Sensor();

}  // namespace air360
