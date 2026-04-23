#include "air360/sensors/sensor_registry.hpp"

#include <string>

#include "air360/sensors/bus_config.hpp"
#include "air360/sensors/drivers/bme280_sensor.hpp"
#include "air360/sensors/drivers/bme680_sensor.hpp"
#include "air360/sensors/drivers/dht_sensor.hpp"
#include "air360/sensors/drivers/ds18b20_sensor.hpp"
#include "air360/sensors/drivers/gps_nmea_sensor.hpp"
#include "air360/sensors/drivers/htu2x_sensor.hpp"
#include "air360/sensors/drivers/me3_no2_sensor.hpp"
#include "air360/sensors/drivers/scd30_sensor.hpp"
#include "air360/sensors/drivers/sht4x_sensor.hpp"
#include "air360/sensors/drivers/sps30_sensor.hpp"
#include "air360/sensors/drivers/ina219_sensor.hpp"
#include "air360/sensors/drivers/mhz19b_sensor.hpp"
#include "air360/sensors/drivers/veml7700_sensor.hpp"
#include "sdkconfig.h"

namespace air360 {

namespace {

bool validateI2cBusId(const SensorRecord& record, std::string& error) {
    if (record.i2c_bus_id != kPrimaryI2cBus) {
        error = "I2C bus id ";
        error += std::to_string(record.i2c_bus_id);
        error += " is not configured on this build.";
        return false;
    }
    return true;
}

bool validateCommonRecord(const SensorRecord& record, std::string& error) {
    if (record.id == 0U) {
        error = "Sensor id must not be zero.";
        return false;
    }

    if (record.poll_interval_ms < 5000U || record.poll_interval_ms > 3600000U) {
        error = "Poll interval must be between 5000 ms and 3600000 ms.";
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

    if (!validateI2cBusId(record, error)) {
        return false;
    }

    if (record.i2c_address != 0x76U && record.i2c_address != 0x77U) {
        error = "BME280 I2C address must be 0x76 or 0x77.";
        return false;
    }

    return true;
}

bool validateBme680Record(const SensorRecord& record, std::string& error) {
    if (!validateCommonRecord(record, error)) {
        return false;
    }

    if (record.transport_kind != TransportKind::kI2c) {
        error = "BME680 currently supports only I2C.";
        return false;
    }

    if (!validateI2cBusId(record, error)) {
        return false;
    }

    if (record.i2c_address != 0x76U && record.i2c_address != 0x77U) {
        error = "BME680 I2C address must be 0x76 or 0x77.";
        return false;
    }

    return true;
}

bool validateSps30Record(const SensorRecord& record, std::string& error) {
    if (!validateCommonRecord(record, error)) {
        return false;
    }

    if (record.transport_kind != TransportKind::kI2c) {
        error = "SPS30 currently supports only I2C.";
        return false;
    }

    if (!validateI2cBusId(record, error)) {
        return false;
    }

    if (record.i2c_address != 0x69U) {
        error = "SPS30 I2C address must be 0x69.";
        return false;
    }

    return true;
}

bool validateScd30Record(const SensorRecord& record, std::string& error) {
    if (!validateCommonRecord(record, error)) {
        return false;
    }

    if (record.transport_kind != TransportKind::kI2c) {
        error = "SCD30 currently supports only I2C.";
        return false;
    }

    if (!validateI2cBusId(record, error)) {
        return false;
    }

    if (record.i2c_address != 0x61U) {
        error = "SCD30 I2C address must be 0x61.";
        return false;
    }

    return true;
}

bool validateVeml7700Record(const SensorRecord& record, std::string& error) {
    if (!validateCommonRecord(record, error)) {
        return false;
    }

    if (record.transport_kind != TransportKind::kI2c) {
        error = "VEML7700 currently supports only I2C.";
        return false;
    }

    if (!validateI2cBusId(record, error)) {
        return false;
    }

    if (record.i2c_address != 0x10U) {
        error = "VEML7700 I2C address must be 0x10.";
        return false;
    }

    return true;
}

bool validateHtu2xRecord(const SensorRecord& record, std::string& error) {
    if (!validateCommonRecord(record, error)) {
        return false;
    }

    if (record.transport_kind != TransportKind::kI2c) {
        error = "HTU2X currently supports only I2C.";
        return false;
    }

    if (!validateI2cBusId(record, error)) {
        return false;
    }

    if (record.i2c_address != 0x40U) {
        error = "HTU2X I2C address must be 0x40.";
        return false;
    }

    return true;
}

bool validateSht4xRecord(const SensorRecord& record, std::string& error) {
    if (!validateCommonRecord(record, error)) {
        return false;
    }

    if (record.transport_kind != TransportKind::kI2c) {
        error = "SHT4X currently supports only I2C.";
        return false;
    }

    if (!validateI2cBusId(record, error)) {
        return false;
    }

    if (record.i2c_address != 0x44U) {
        error = "SHT4X I2C address must be 0x44.";
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
        error = "GPS UART port, RX, and TX pins must match the build-time board configuration.";
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
    return validateDhtRecord(record, error, 2000U);
}

bool validateDht22Record(const SensorRecord& record, std::string& error) {
    return validateDhtRecord(record, error, 2000U);
}

bool validateDs18b20Record(const SensorRecord& record, std::string& error) {
    if (!validateCommonRecord(record, error)) {
        return false;
    }

    if (record.transport_kind != TransportKind::kGpio) {
        error = "DS18B20 currently supports only GPIO / 1-Wire transport.";
        return false;
    }

    if (record.analog_gpio_pin != CONFIG_AIR360_GPIO_SENSOR_PIN_0 &&
        record.analog_gpio_pin != CONFIG_AIR360_GPIO_SENSOR_PIN_1 &&
        record.analog_gpio_pin != CONFIG_AIR360_GPIO_SENSOR_PIN_2) {
        error = "GPIO pin must match one of the board sensor GPIO slots.";
        return false;
    }

    return true;
}

bool validateIna219Record(const SensorRecord& record, std::string& error) {
    if (!validateCommonRecord(record, error)) {
        return false;
    }

    if (record.transport_kind != TransportKind::kI2c) {
        error = "INA219 currently supports only I2C.";
        return false;
    }

    if (!validateI2cBusId(record, error)) {
        return false;
    }

    if (record.i2c_address != 0x40U && record.i2c_address != 0x41U &&
        record.i2c_address != 0x44U && record.i2c_address != 0x45U) {
        error = "INA219 I2C address must be 0x40, 0x41, 0x44, or 0x45.";
        return false;
    }

    return true;
}

bool validateMhz19bRecord(const SensorRecord& record, std::string& error) {
    if (!validateCommonRecord(record, error)) {
        return false;
    }

    if (record.transport_kind != TransportKind::kUart) {
        error = "MH-Z19B currently supports only UART.";
        return false;
    }

    if (record.uart_baud_rate != 9600U) {
        error = "MH-Z19B requires UART baud rate of 9600.";
        return false;
    }

    return true;
}

bool validateMe3No2Record(const SensorRecord& record, std::string& error) {
    if (!validateCommonRecord(record, error)) {
        return false;
    }

    if (record.transport_kind != TransportKind::kAnalog) {
        error = "ME3-NO2 currently supports only analog transport.";
        return false;
    }

    if (record.analog_gpio_pin != CONFIG_AIR360_GPIO_SENSOR_PIN_0 &&
        record.analog_gpio_pin != CONFIG_AIR360_GPIO_SENSOR_PIN_1 &&
        record.analog_gpio_pin != CONFIG_AIR360_GPIO_SENSOR_PIN_2) {
        error = "Analog pin must match one of the board sensor GPIO slots.";
        return false;
    }

    return true;
}

// Guard: fails if SensorDescriptor gains or loses fields, forcing registry updates.
// Size computed for ESP32 (32-bit, 4-byte pointers): 17 fields, 44 bytes with padding.
static_assert(sizeof(SensorDescriptor) == 44U,
    "SensorDescriptor layout changed — update kDescriptors[] designated initializers");

constexpr SensorDescriptor kDescriptors[] = {
    {
        .type                     = SensorType::kBme280,
        .type_key                 = "bme280",
        .display_name             = "BME280",
        .supports_i2c             = true,
        .supports_analog          = false,
        .supports_uart            = false,
        .supports_gpio            = false,
        .driver_implemented       = true,
        .default_poll_interval_ms = 5000U,
        .default_i2c_bus_id       = kPrimaryI2cBus,
        .default_i2c_address      = 0x76U,
        .default_uart_port_id     = 0U,
        .default_uart_rx_gpio_pin = -1,
        .default_uart_tx_gpio_pin = -1,
        .default_uart_baud_rate   = 0U,
        .validate                 = &validateBme280Record,
        .create_driver            = &createBme280Sensor,
    },
    {
        .type                     = SensorType::kBme680,
        .type_key                 = "bme680",
        .display_name             = "BME680",
        .supports_i2c             = true,
        .supports_analog          = false,
        .supports_uart            = false,
        .supports_gpio            = false,
        .driver_implemented       = true,
        .default_poll_interval_ms = 5000U,
        .default_i2c_bus_id       = kPrimaryI2cBus,
        .default_i2c_address      = 0x77U,
        .default_uart_port_id     = 0U,
        .default_uart_rx_gpio_pin = -1,
        .default_uart_tx_gpio_pin = -1,
        .default_uart_baud_rate   = 0U,
        .validate                 = &validateBme680Record,
        .create_driver            = &createBme680Sensor,
    },
    {
        .type                     = SensorType::kSps30,
        .type_key                 = "sps30",
        .display_name             = "SPS30",
        .supports_i2c             = true,
        .supports_analog          = false,
        .supports_uart            = false,
        .supports_gpio            = false,
        .driver_implemented       = true,
        .default_poll_interval_ms = 5000U,
        .default_i2c_bus_id       = kPrimaryI2cBus,
        .default_i2c_address      = 0x69U,
        .default_uart_port_id     = 0U,
        .default_uart_rx_gpio_pin = -1,
        .default_uart_tx_gpio_pin = -1,
        .default_uart_baud_rate   = 0U,
        .validate                 = &validateSps30Record,
        .create_driver            = &createSps30Sensor,
    },
    {
        .type                     = SensorType::kScd30,
        .type_key                 = "scd30",
        .display_name             = "SCD30",
        .supports_i2c             = true,
        .supports_analog          = false,
        .supports_uart            = false,
        .supports_gpio            = false,
        .driver_implemented       = true,
        .default_poll_interval_ms = 5000U,
        .default_i2c_bus_id       = kPrimaryI2cBus,
        .default_i2c_address      = 0x61U,
        .default_uart_port_id     = 0U,
        .default_uart_rx_gpio_pin = -1,
        .default_uart_tx_gpio_pin = -1,
        .default_uart_baud_rate   = 0U,
        .validate                 = &validateScd30Record,
        .create_driver            = &createScd30Sensor,
    },
    {
        .type                     = SensorType::kVeml7700,
        .type_key                 = "veml7700",
        .display_name             = "VEML7700",
        .supports_i2c             = true,
        .supports_analog          = false,
        .supports_uart            = false,
        .supports_gpio            = false,
        .driver_implemented       = true,
        .default_poll_interval_ms = 5000U,
        .default_i2c_bus_id       = kPrimaryI2cBus,
        .default_i2c_address      = 0x10U,
        .default_uart_port_id     = 0U,
        .default_uart_rx_gpio_pin = -1,
        .default_uart_tx_gpio_pin = -1,
        .default_uart_baud_rate   = 0U,
        .validate                 = &validateVeml7700Record,
        .create_driver            = &createVeml7700Sensor,
    },
    {
        .type                     = SensorType::kGpsNmea,
        .type_key                 = "gps_nmea",
        .display_name             = "GPS (NMEA)",
        .supports_i2c             = false,
        .supports_analog          = false,
        .supports_uart            = true,
        .supports_gpio            = false,
        .driver_implemented       = true,
        .default_poll_interval_ms = 5000U,
        .default_i2c_bus_id       = kPrimaryI2cBus,
        .default_i2c_address      = 0x00U,
        .default_uart_port_id     = CONFIG_AIR360_GPS_DEFAULT_UART_PORT,
        .default_uart_rx_gpio_pin = CONFIG_AIR360_GPS_DEFAULT_RX_GPIO,
        .default_uart_tx_gpio_pin = CONFIG_AIR360_GPS_DEFAULT_TX_GPIO,
        .default_uart_baud_rate   = CONFIG_AIR360_GPS_DEFAULT_BAUD_RATE,
        .validate                 = &validateGpsNmeaRecord,
        .create_driver            = &createGpsNmeaSensor,
    },
    {
        .type                     = SensorType::kDht11,
        .type_key                 = "dht11",
        .display_name             = "DHT11",
        .supports_i2c             = false,
        .supports_analog          = false,
        .supports_uart            = false,
        .supports_gpio            = true,
        .driver_implemented       = true,
        .default_poll_interval_ms = 5000U,
        .default_i2c_bus_id       = kPrimaryI2cBus,
        .default_i2c_address      = 0x00U,
        .default_uart_port_id     = 0U,
        .default_uart_rx_gpio_pin = -1,
        .default_uart_tx_gpio_pin = -1,
        .default_uart_baud_rate   = 0U,
        .validate                 = &validateDht11Record,
        .create_driver            = &createDht11Sensor,
    },
    {
        .type                     = SensorType::kDht22,
        .type_key                 = "dht22",
        .display_name             = "DHT22",
        .supports_i2c             = false,
        .supports_analog          = false,
        .supports_uart            = false,
        .supports_gpio            = true,
        .driver_implemented       = true,
        .default_poll_interval_ms = 5000U,
        .default_i2c_bus_id       = kPrimaryI2cBus,
        .default_i2c_address      = 0x00U,
        .default_uart_port_id     = 0U,
        .default_uart_rx_gpio_pin = -1,
        .default_uart_tx_gpio_pin = -1,
        .default_uart_baud_rate   = 0U,
        .validate                 = &validateDht22Record,
        .create_driver            = &createDht22Sensor,
    },
    {
        .type                     = SensorType::kDs18b20,
        .type_key                 = "ds18b20",
        .display_name             = "DS18B20",
        .supports_i2c             = false,
        .supports_analog          = false,
        .supports_uart            = false,
        .supports_gpio            = true,
        .driver_implemented       = true,
        .default_poll_interval_ms = 5000U,
        .default_i2c_bus_id       = kPrimaryI2cBus,
        .default_i2c_address      = 0x00U,
        .default_uart_port_id     = 0U,
        .default_uart_rx_gpio_pin = -1,
        .default_uart_tx_gpio_pin = -1,
        .default_uart_baud_rate   = 0U,
        .validate                 = &validateDs18b20Record,
        .create_driver            = &createDs18b20Sensor,
    },
    {
        .type                     = SensorType::kHtu2x,
        .type_key                 = "htu2x",
        .display_name             = "HTU2X",
        .supports_i2c             = true,
        .supports_analog          = false,
        .supports_uart            = false,
        .supports_gpio            = false,
        .driver_implemented       = true,
        .default_poll_interval_ms = 5000U,
        .default_i2c_bus_id       = kPrimaryI2cBus,
        .default_i2c_address      = 0x40U,
        .default_uart_port_id     = 0U,
        .default_uart_rx_gpio_pin = -1,
        .default_uart_tx_gpio_pin = -1,
        .default_uart_baud_rate   = 0U,
        .validate                 = &validateHtu2xRecord,
        .create_driver            = &createHtu2xSensor,
    },
    {
        .type                     = SensorType::kSht4x,
        .type_key                 = "sht4x",
        .display_name             = "SHT4X",
        .supports_i2c             = true,
        .supports_analog          = false,
        .supports_uart            = false,
        .supports_gpio            = false,
        .driver_implemented       = true,
        .default_poll_interval_ms = 5000U,
        .default_i2c_bus_id       = kPrimaryI2cBus,
        .default_i2c_address      = 0x44U,
        .default_uart_port_id     = 0U,
        .default_uart_rx_gpio_pin = -1,
        .default_uart_tx_gpio_pin = -1,
        .default_uart_baud_rate   = 0U,
        .validate                 = &validateSht4xRecord,
        .create_driver            = &createSht4xSensor,
    },
    {
        .type                     = SensorType::kIna219,
        .type_key                 = "ina219",
        .display_name             = "INA219",
        .supports_i2c             = true,
        .supports_analog          = false,
        .supports_uart            = false,
        .supports_gpio            = false,
        .driver_implemented       = true,
        .default_poll_interval_ms = 5000U,
        .default_i2c_bus_id       = kPrimaryI2cBus,
        .default_i2c_address      = 0x40U,
        .default_uart_port_id     = 0U,
        .default_uart_rx_gpio_pin = -1,
        .default_uart_tx_gpio_pin = -1,
        .default_uart_baud_rate   = 0U,
        .validate                 = &validateIna219Record,
        .create_driver            = &createIna219Sensor,
    },
    {
        .type                     = SensorType::kMhz19b,
        .type_key                 = "mhz19b",
        .display_name             = "MH-Z19B",
        .supports_i2c             = false,
        .supports_analog          = false,
        .supports_uart            = true,
        .supports_gpio            = false,
        .driver_implemented       = true,
        .default_poll_interval_ms = 10000U,
        .default_i2c_bus_id       = kPrimaryI2cBus,
        .default_i2c_address      = 0x00U,
        .default_uart_port_id     = CONFIG_AIR360_MHZ19B_DEFAULT_UART_PORT,
        .default_uart_rx_gpio_pin = CONFIG_AIR360_MHZ19B_DEFAULT_RX_GPIO,
        .default_uart_tx_gpio_pin = CONFIG_AIR360_MHZ19B_DEFAULT_TX_GPIO,
        .default_uart_baud_rate   = 9600U,
        .validate                 = &validateMhz19bRecord,
        .create_driver            = &createMhz19bSensor,
    },
    {
        .type                     = SensorType::kMe3No2,
        .type_key                 = "me3_no2",
        .display_name             = "ME3-NO2",
        .supports_i2c             = false,
        .supports_analog          = true,
        .supports_uart            = false,
        .supports_gpio            = false,
        .driver_implemented       = true,
        .default_poll_interval_ms = 5000U,
        .default_i2c_bus_id       = kPrimaryI2cBus,
        .default_i2c_address      = 0x00U,
        .default_uart_port_id     = 0U,
        .default_uart_rx_gpio_pin = -1,
        .default_uart_tx_gpio_pin = -1,
        .default_uart_baud_rate   = 0U,
        .validate                 = &validateMe3No2Record,
        .create_driver            = &createMe3No2Sensor,
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
