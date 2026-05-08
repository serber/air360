#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "air360/sensors/sensor_driver.hpp"
#include <TinyGPSPlus.h>
#include "freertos/FreeRTOS.h"

namespace air360 {

class GpsNmeaSensor final : public SensorDriver {
  public:
    GpsNmeaSensor() = default;
    ~GpsNmeaSensor() override = default;

    SensorType type() const override;
    esp_err_t init(
        const SensorRecord& record,
        const SensorDriverContext& context) override;
    esp_err_t poll() override;
    SensorMeasurement latestMeasurement() const override;
    std::string lastError() const override;

  private:
    esp_err_t drainUartEvents();
    std::size_t computeMaxBytesPerPoll() const;
    TickType_t computeReadTimeoutTicks() const;
    void rebuildMeasurement();
    void setError(const std::string& message);
    void resetParser();

    SensorRecord record_{};
    UartPortManager* uart_port_manager_ = nullptr;
    // std::optional re-initializes TinyGPSPlus in place via emplace(), avoiding
    // both heap allocations and placement-new on this hot-init path.
    std::optional<TinyGPSPlus> parser_;
    SensorMeasurement measurement_{};
    std::string last_error_;
    std::size_t max_bytes_per_poll_ = 0U;
    TickType_t read_timeout_ticks_ = 0;
    std::uint32_t uart_overrun_count_ = 0U;
    bool initialized_ = false;
};

std::unique_ptr<SensorDriver> createGpsNmeaSensor();

}  // namespace air360
