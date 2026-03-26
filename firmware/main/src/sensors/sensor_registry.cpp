#include "air360/sensors/sensor_registry.hpp"

#include <cstring>
#include <string>

#include "air360/sensors/drivers/bme280_sensor.hpp"
#include "air360/sensors/drivers/dht_sensor.hpp"
#include "air360/sensors/drivers/gps_nmea_sensor.hpp"
#include "sdkconfig.h"

#ifndef CONFIG_AIR360_GPS_DEFAULT_UART_PORT
#define CONFIG_AIR360_GPS_DEFAULT_UART_PORT 1
#endif

#ifndef CONFIG_AIR360_GPS_DEFAULT_RX_GPIO
#define CONFIG_AIR360_GPS_DEFAULT_RX_GPIO 44
#endif

#ifndef CONFIG_AIR360_GPS_DEFAULT_TX_GPIO
#define CONFIG_AIR360_GPS_DEFAULT_TX_GPIO 43
#endif

#ifndef CONFIG_AIR360_GPIO_SENSOR_PIN_0
#define CONFIG_AIR360_GPIO_SENSOR_PIN_0 4
#endif

#ifndef CONFIG_AIR360_GPIO_SENSOR_PIN_1
#define CONFIG_AIR360_GPIO_SENSOR_PIN_1 5
#endif

#ifndef CONFIG_AIR360_GPIO_SENSOR_PIN_2
#define CONFIG_AIR360_GPIO_SENSOR_PIN_2 6
#endif

namespace air360 {

namespace {

bool validateCommonRecord(const SensorRecord& record, std::string& error) {
    if (record.id == 0U) {
        error = "Sensor id must not be zero.";
        return false;
    }

    if (record.display_name[0] == '\0') {
        error = "Sensor display name must not be empty.";
        return false;
    }

    if (record.display_name[kSensorDisplayNameCapacity - 1U] != '\0') {
        error = "Sensor display name is not null-terminated.";
        return false;
    }

    const std::size_t display_name_length = std::strlen(record.display_name);
    if (display_name_length == 0U || display_name_length >= kSensorDisplayNameCapacity) {
        error = "Sensor display name is invalid.";
        return false;
    }

    if (record.poll_interval_ms < 1000U || record.poll_interval_ms > 3600000U) {
        error = "Poll interval must be between 1000 ms and 3600000 ms.";
        return false;
    }

    return true;
}

bool validateBme280Record(const SensorRecord& record, std::string& error) {
    if (!validateCommonRecord(record, error)) {
        return false;
    }

    if (record.transport_kind != TransportKind::kI2c) {
        error = "BME280 currently supports only I2C.";
        return false;
    }

    if (record.i2c_bus_id > 1U) {
        error = "I2C bus id must be 0 or 1.";
        return false;
    }

    if (record.i2c_address < 0x03U || record.i2c_address > 0x77U) {
        error = "I2C address must be between 0x03 and 0x77.";
        return false;
    }

    return true;
}

bool validateGpsNmeaRecord(const SensorRecord& record, std::string& error) {
    if (!validateCommonRecord(record, error)) {
        return false;
    }

    if (record.transport_kind != TransportKind::kUart) {
        error = "GPS currently supports only UART.";
        return false;
    }

    if (record.uart_port_id != CONFIG_AIR360_GPS_DEFAULT_UART_PORT ||
        record.uart_rx_gpio_pin != CONFIG_AIR360_GPS_DEFAULT_RX_GPIO ||
        record.uart_tx_gpio_pin != CONFIG_AIR360_GPS_DEFAULT_TX_GPIO) {
        error = "GPS UART binding must match the fixed board wiring.";
        return false;
    }

    if (record.uart_baud_rate < 1200U || record.uart_baud_rate > 115200U) {
        error = "UART baud rate must be between 1200 and 115200.";
        return false;
    }

    return true;
}

bool validateDhtRecord(const SensorRecord& record, std::string& error, std::uint32_t min_poll_interval_ms) {
    if (!validateCommonRecord(record, error)) {
        return false;
    }

    if (record.transport_kind != TransportKind::kGpio) {
        error = "DHT currently supports only GPIO transport.";
        return false;
    }

    if (record.analog_gpio_pin != CONFIG_AIR360_GPIO_SENSOR_PIN_0 &&
        record.analog_gpio_pin != CONFIG_AIR360_GPIO_SENSOR_PIN_1 &&
        record.analog_gpio_pin != CONFIG_AIR360_GPIO_SENSOR_PIN_2) {
        error = "GPIO pin must match one of the board sensor GPIO slots.";
        return false;
    }

    if (record.poll_interval_ms < min_poll_interval_ms) {
        error = "Poll interval is too short for selected DHT sensor.";
        return false;
    }

    return true;
}

bool validateDht11Record(const SensorRecord& record, std::string& error) {
    return validateDhtRecord(record, error, 1000U);
}

bool validateDht22Record(const SensorRecord& record, std::string& error) {
    return validateDhtRecord(record, error, 2000U);
}

constexpr SensorDescriptor kDescriptors[] = {
    {
        SensorType::kBme280,
        "bme280",
        "BME280",
        true,
        false,
        false,
        false,
        true,
        10000U,
        0U,
        0x76U,
        &validateBme280Record,
        &createBme280Sensor,
    },
    {
        SensorType::kGpsNmea,
        "gps_nmea",
        "GPS (NMEA)",
        false,
        false,
        true,
        false,
        true,
        2000U,
        0U,
        0x00U,
        &validateGpsNmeaRecord,
        &createGpsNmeaSensor,
    },
    {
        SensorType::kDht11,
        "dht11",
        "DHT11",
        false,
        false,
        false,
        true,
        true,
        2000U,
        0U,
        0x00U,
        &validateDht11Record,
        &createDht11Sensor,
    },
    {
        SensorType::kDht22,
        "dht22",
        "DHT22",
        false,
        false,
        false,
        true,
        true,
        2000U,
        0U,
        0x00U,
        &validateDht22Record,
        &createDht22Sensor,
    },
};

}  // namespace

const SensorDescriptor* SensorRegistry::descriptors() const {
    return kDescriptors;
}

std::size_t SensorRegistry::descriptorCount() const {
    return sizeof(kDescriptors) / sizeof(kDescriptors[0]);
}

const SensorDescriptor* SensorRegistry::findByType(SensorType type) const {
    for (const auto& descriptor : kDescriptors) {
        if (descriptor.type == type) {
            return &descriptor;
        }
    }

    return nullptr;
}

const SensorDescriptor* SensorRegistry::findByTypeKey(const std::string& type_key) const {
    for (const auto& descriptor : kDescriptors) {
        if (type_key == descriptor.type_key) {
            return &descriptor;
        }
    }

    return nullptr;
}

bool SensorRegistry::supportsTransport(
    const SensorDescriptor& descriptor,
    TransportKind kind) const {
    switch (kind) {
        case TransportKind::kI2c:
            return descriptor.supports_i2c;
        case TransportKind::kAnalog:
            return descriptor.supports_analog;
        case TransportKind::kUart:
            return descriptor.supports_uart;
        case TransportKind::kGpio:
            return descriptor.supports_gpio;
        case TransportKind::kUnknown:
        default:
            return false;
    }
}

bool SensorRegistry::validateRecord(const SensorRecord& record, std::string& error) const {
    const SensorDescriptor* descriptor = findByType(record.sensor_type);
    if (descriptor == nullptr) {
        error = "Unsupported sensor type.";
        return false;
    }

    if (!supportsTransport(*descriptor, record.transport_kind)) {
        error = "Selected transport is not supported for this sensor.";
        return false;
    }

    if (descriptor->validate == nullptr) {
        error.clear();
        return true;
    }

    return descriptor->validate(record, error);
}

}  // namespace air360
