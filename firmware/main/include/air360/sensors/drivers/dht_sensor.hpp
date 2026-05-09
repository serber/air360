#pragma once

#include <cstdint>
#include <memory>

#include "air360/sensors/sensor_driver.hpp"
#include "dht.h"

namespace air360 {

enum class DhtModel : std::uint8_t {
    kDht11 = 0U,
    kDht22 = 1U,
};

class DhtSensor final : public SensorDriver {
  public:
    explicit DhtSensor(DhtModel model);
    ~DhtSensor() override;

    SensorType type() const override;
    esp_err_t init(
        const SensorRecord& record,
        const SensorDriverContext& context) override;
    esp_err_t poll() override;
    SensorMeasurement latestMeasurement() const override;

  private:
    DhtModel model_;
    SensorRecord record_{};
    SensorMeasurement measurement_{};
};

std::unique_ptr<SensorDriver> createDht11Sensor();
std::unique_ptr<SensorDriver> createDht22Sensor();

}  // namespace air360
