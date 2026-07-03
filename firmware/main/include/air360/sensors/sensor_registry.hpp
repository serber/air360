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
constexpr std::size_t kMaxGpioPinsPerSensor = 3U;
inline constexpr std::array<std::int16_t, kMaxGpioPinsPerSensor>
    kBoardSensorGpioPins{{4, 5, 6}};
inline constexpr std::uint8_t kBoardSensorGpioPinCount =
    static_cast<std::uint8_t>(kBoardSensorGpioPins.size());

struct SensorUartPortBinding {
    std::uint8_t port_id;
    std::int16_t rx_gpio_pin;
    std::int16_t tx_gpio_pin;
};

// A single one-shot maintenance action a sensor type advertises. key is the
// stable form/JSON token (matches maintenanceActionKey()); label is the UI text
// for the "run on next boot" selector. The driver implements the action.
struct MaintenanceActionDescriptor {
    MaintenanceActionKind kind;
    const char* key;
    const char* label;
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
    std::array<std::int16_t, kMaxGpioPinsPerSensor> allowed_gpio_pins;
    std::uint8_t allowed_gpio_pin_count;
    SensorValidationFn validate;
    SensorDriverFactory create_driver;
    // Startup calibration capability. When true, the web UI shows a calibration
    // checkbox for this sensor type and the driver acts on
    // SensorRecord::startup_calibration inside init(). calibration_label is the
    // UI label for that checkbox. Any calibration tied to this flag MUST be
    // idempotent because init()/re-init() can run on every boot. SCD30 maps the
    // flag to enabling/disabling automatic self-calibration (ASC).
    bool supports_startup_calibration = false;
    const char* calibration_label = nullptr;
    // One-shot maintenance actions this sensor supports, run once after boot and
    // then cleared (run-once). The web UI offers them in a "run on next boot"
    // selector; the driver implements each as a non-blocking poll() state
    // machine. Stored as a pointer to a static constexpr array to keep the
    // descriptor small. Distinct from the persistent startup_calibration mode.
    const MaintenanceActionDescriptor* maintenance_actions = nullptr;
    std::uint8_t maintenance_action_count = 0U;
};

inline std::int16_t firstAllowedGpioPin(const SensorDescriptor& descriptor) {
    return descriptor.allowed_gpio_pin_count > 0U ? descriptor.allowed_gpio_pins[0] : -1;
}

// Looks up a maintenance action by its stable key (form/JSON token). Returns
// nullptr when the sensor does not advertise an action with that key.
inline const MaintenanceActionDescriptor* findMaintenanceActionByKey(
    const SensorDescriptor& descriptor, const std::string& key) {
    for (std::uint8_t index = 0U; index < descriptor.maintenance_action_count; ++index) {
        if (key == descriptor.maintenance_actions[index].key) {
            return &descriptor.maintenance_actions[index];
        }
    }
    return nullptr;
}

// True when the sensor advertises an action whose kind matches the persisted
// pending_maintenance_action byte (0/kNone always passes as "nothing pending").
inline bool sensorSupportsMaintenanceActionValue(
    const SensorDescriptor& descriptor, std::uint8_t pending_action) {
    if (pending_action == static_cast<std::uint8_t>(MaintenanceActionKind::kNone)) {
        return true;
    }
    for (std::uint8_t index = 0U; index < descriptor.maintenance_action_count; ++index) {
        if (static_cast<std::uint8_t>(descriptor.maintenance_actions[index].kind) ==
            pending_action) {
            return true;
        }
    }
    return false;
}

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
