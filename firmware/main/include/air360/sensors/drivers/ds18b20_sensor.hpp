#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "air360/sensors/sensor_driver.hpp"

typedef struct onewire_bus_t* onewire_bus_handle_t;
typedef struct ds18b20_device_t* ds18b20_device_handle_t;
typedef std::uint64_t onewire_device_address_t;

namespace air360 {

class Ds18b20Sensor final : public SensorDriver {
  public:
    Ds18b20Sensor() = default;
    ~Ds18b20Sensor() override;

    SensorType type() const override;
    esp_err_t init(
        const SensorRecord& record,
        const SensorDriverContext& context) override;
    esp_err_t poll() override;
    SensorMeasurement latestMeasurement() const override;
    std::string lastError() const override;

  private:
    void reset();
    void setError(const std::string& message);

    SensorRecord record_{};
    onewire_bus_handle_t bus_ = nullptr;
    ds18b20_device_handle_t device_ = nullptr;
    onewire_device_address_t address_ = 0U;
    SensorMeasurement measurement_{};
    std::string last_error_;
    std::uint32_t poll_failure_count_ = 0U;
    bool initialized_ = false;
};

std::unique_ptr<SensorDriver> createDs18b20Sensor();

}  // namespace air360
