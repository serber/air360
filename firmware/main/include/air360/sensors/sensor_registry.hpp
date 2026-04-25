#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "air360/sensors/sensor_config.hpp"
#include "air360/sensors/sensor_driver.hpp"

namespace air360 {

using SensorValidationFn = bool (*)(const SensorRecord& record, std::string& error);
using SensorDriverFactory = std::unique_ptr<SensorDriver> (*)();

constexpr std::size_t kMaxI2cAddressesPerSensor = 4U;
constexpr std::size_t kMaxUartPortsPerSensor = 2U;

struct SensorUartPortBinding {
    std::uint8_t port_id;
    std::int16_t rx_gpio_pin;
    std::int16_t tx_gpio_pin;
};

inline constexpr std::array<SensorUartPortBinding, kMaxUartPortsPerSensor>
    kSensorUartPortBindings{{
        {1U, 18, 17},
        {2U, 16, 15},
    }};

inline const SensorUartPortBinding* findSensorUartPortBinding(std::uint8_t port_id) {
    for (const auto& binding : kSensorUartPortBindings) {
        if (binding.port_id == port_id) {
            return &binding;
        }
    }
    return nullptr;
}

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
    std::array<std::uint8_t, kMaxI2cAddressesPerSensor> allowed_i2c_addresses;
    std::uint8_t allowed_i2c_address_count;
    std::uint8_t default_uart_port_id;
    std::array<std::uint8_t, kMaxUartPortsPerSensor> allowed_uart_ports;
    std::uint8_t allowed_uart_port_count;
    std::int16_t default_uart_rx_gpio_pin;
    std::int16_t default_uart_tx_gpio_pin;
    std::uint32_t default_uart_baud_rate;
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
