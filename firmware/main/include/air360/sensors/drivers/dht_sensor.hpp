#pragma once

#include <memory>
#include <string>

#include "air360/sensors/sensor_driver.hpp"

namespace air360 {

enum class DhtModel : std::uint8_t {
    kDht11 = 0U,
    kDht22 = 1U,
};

class DhtSensor final : public SensorDriver {
  public:
    explicit DhtSensor(DhtModel model);

    SensorType type() const override;
    esp_err_t init(
        const SensorRecord& record,
        const SensorDriverContext& context) override;
    esp_err_t poll() override;
    SensorMeasurement latestMeasurement() const override;
    std::string lastError() const override;

  private:
    esp_err_t readRawFrame(std::uint8_t data[5]);
    bool decodeFrame(const std::uint8_t data[5], float& temperature_c, float& humidity_percent) const;
    void setError(const std::string& message);

    DhtModel model_;
    SensorRecord record_{};
    SensorMeasurement measurement_{};
    std::string last_error_;
    bool initialized_ = false;
};

std::unique_ptr<SensorDriver> createDht11Sensor();
std::unique_ptr<SensorDriver> createDht22Sensor();

}  // namespace air360
