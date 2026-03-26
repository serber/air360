#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "air360/sensors/sensor_config.hpp"
#include "air360/sensors/sensor_driver.hpp"

namespace air360 {

using SensorValidationFn = bool (*)(const SensorRecord& record, std::string& error);
using SensorDriverFactory = std::unique_ptr<SensorDriver> (*)();

struct SensorDescriptor {
    SensorType type;
    const char* type_key;
    const char* display_name;
    bool supports_i2c;
    bool supports_analog;
    bool supports_uart;
    bool supports_gpio;
    bool driver_implemented;
    std::uint32_t default_poll_interval_ms;
    std::uint8_t default_i2c_bus_id;
    std::uint8_t default_i2c_address;
    SensorValidationFn validate;
    SensorDriverFactory create_driver;
};

class SensorRegistry {
  public:
    const SensorDescriptor* descriptors() const;
    std::size_t descriptorCount() const;
    const SensorDescriptor* findByType(SensorType type) const;
    const SensorDescriptor* findByTypeKey(const std::string& type_key) const;
    bool supportsTransport(const SensorDescriptor& descriptor, TransportKind kind) const;
    bool validateRecord(const SensorRecord& record, std::string& error) const;
};

}  // namespace air360
