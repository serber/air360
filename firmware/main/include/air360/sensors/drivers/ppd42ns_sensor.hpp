#pragma once

#include <cstdint>
#include <memory>

#include "air360/sensors/sensor_driver.hpp"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"

namespace air360 {

class Ppd42nsSensor final : public SensorDriver {
  public:
    Ppd42nsSensor();
    ~Ppd42nsSensor() override;

    SensorType type() const override;
    esp_err_t init(
        const SensorRecord& record,
        const SensorDriverContext& context) override;
    esp_err_t poll() override;
    SensorMeasurement latestMeasurement() const override;

  private:
    static void IRAM_ATTR isrHandler(void* arg);

    void handleEdgeFromIsr(std::uint64_t now_us, int level);
    void resetAccumulation(std::uint64_t now_us);
    esp_err_t detachInterrupt();

    SensorRecord record_{};
    SensorMeasurement measurement_{};
    portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
    std::uint64_t low_start_us_ = 0U;
    std::uint64_t accumulated_low_us_ = 0U;
    std::uint64_t sample_start_us_ = 0U;
    std::uint64_t initialized_at_us_ = 0U;
    gpio_num_t gpio_pin_ = GPIO_NUM_NC;
    bool isr_attached_ = false;
};

std::unique_ptr<SensorDriver> createPpd42nsSensor();

}  // namespace air360
