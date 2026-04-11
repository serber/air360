#include "air360/sensors/sensor_registry.hpp"

#include <string>

#include "air360/sensors/drivers/bme280_sensor.hpp"
#include "air360/sensors/drivers/bme680_sensor.hpp"
#include "air360/sensors/drivers/dht_sensor.hpp"
#include "air360/sensors/drivers/ds18b20_sensor.hpp"
#include "air360/sensors/drivers/ens160_sensor.hpp"
#include "air360/sensors/drivers/gps_nmea_sensor.hpp"
#include "air360/sensors/drivers/htu2x_sensor.hpp"
#include "air360/sensors/drivers/me3_no2_sensor.hpp"
#include "air360/sensors/drivers/scd30_sensor.hpp"
#include "air360/sensors/drivers/sht4x_sensor.hpp"
#include "air360/sensors/drivers/sps30_sensor.hpp"
#include "air360/sensors/drivers/veml7700_sensor.hpp"
#include "sdkconfig.h"

#ifndef CONFIG_AIR360_GPS_DEFAULT_UART_PORT
#define CONFIG_AIR360_GPS_DEFAULT_UART_PORT 1
#endif

#ifndef CONFIG_AIR360_GPS_DEFAULT_RX_GPIO
#define CONFIG_AIR360_GPS_DEFAULT_RX_GPIO 18
#endif

#ifndef CONFIG_AIR360_GPS_DEFAULT_TX_GPIO
#define CONFIG_AIR360_GPS_DEFAULT_TX_GPIO 17
#endif

#ifndef CONFIG_AIR360_GPS_DEFAULT_BAUD_RATE
#define CONFIG_AIR360_GPS_DEFAULT_BAUD_RATE 9600
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

    if (record.i2c_bus_id != 0U) {
        error = "I2C bus id must be 0 for the current board wiring.";
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

    if (record.i2c_bus_id != 0U) {
        error = "I2C bus id must be 0 for the current board wiring.";
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

    if (record.i2c_bus_id != 0U) {
        error = "I2C bus id must be 0 for the current board wiring.";
        return false;
    }

    if (record.i2c_address != 0x69U) {
        error = "SPS30 I2C address must be 0x69.";
        return false;
    }

    return true;
}

bool validateEns160Record(const SensorRecord& record, std::string& error) {
    if (!validateCommonRecord(record, error)) {
        return false;
    }

    if (record.transport_kind != TransportKind::kI2c) {
        error = "ENS160 currently supports only I2C.";
        return false;
    }

    if (record.i2c_bus_id != 0U) {
        error = "I2C bus id must be 0 for the current board wiring.";
        return false;
    }

    if (record.i2c_address != 0x52U && record.i2c_address != 0x53U) {
        error = "ENS160 I2C address must be 0x52 or 0x53.";
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

    if (record.i2c_bus_id != 0U) {
        error = "I2C bus id must be 0 for the current board wiring.";
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

    if (record.i2c_bus_id != 0U) {
        error = "I2C bus id must be 0 for the current board wiring.";
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

    if (record.i2c_bus_id != 0U) {
        error = "I2C bus id must be 0 for the current board wiring.";
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

    if (record.i2c_bus_id != 0U) {
        error = "I2C bus id must be 0 for the current board wiring.";
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
        0U,
        -1,
        -1,
        0U,
        &validateBme280Record,
        &createBme280Sensor,
    },
    {
        SensorType::kBme680,
        "bme680",
        "BME680",
        true,
        false,
        false,
        false,
        true,
        5000U,
        0U,
        0x77U,
        0U,
        -1,
        -1,
        0U,
        &validateBme680Record,
        &createBme680Sensor,
    },
    {
        SensorType::kSps30,
        "sps30",
        "SPS30",
        true,
        false,
        false,
        false,
        true,
        5000U,
        0U,
        0x69U,
        0U,
        -1,
        -1,
        0U,
        &validateSps30Record,
        &createSps30Sensor,
    },
    {
        SensorType::kEns160,
        "ens160",
        "ENS160",
        true,
        false,
        false,
        false,
        true,
        5000U,
        0U,
        0x52U,
        0U,
        -1,
        -1,
        0U,
        &validateEns160Record,
        &createEns160Sensor,
    },
    {
        SensorType::kScd30,
        "scd30",
        "SCD30",
        true,
        false,
        false,
        false,
        true,
        5000U,
        0U,
        0x61U,
        0U,
        -1,
        -1,
        0U,
        &validateScd30Record,
        &createScd30Sensor,
    },
    {
        SensorType::kVeml7700,
        "veml7700",
        "VEML7700",
        true,
        false,
        false,
        false,
        true,
        5000U,
        0U,
        0x10U,
        0U,
        -1,
        -1,
        0U,
        &validateVeml7700Record,
        &createVeml7700Sensor,
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
        5000U,
        0U,
        0x00U,
        CONFIG_AIR360_GPS_DEFAULT_UART_PORT,
        CONFIG_AIR360_GPS_DEFAULT_RX_GPIO,
        CONFIG_AIR360_GPS_DEFAULT_TX_GPIO,
        CONFIG_AIR360_GPS_DEFAULT_BAUD_RATE,
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
        5000U,
        0U,
        0x00U,
        0U,
        -1,
        -1,
        0U,
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
        5000U,
        0U,
        0x00U,
        0U,
        -1,
        -1,
        0U,
        &validateDht22Record,
        &createDht22Sensor,
    },
    {
        SensorType::kDs18b20,
        "ds18b20",
        "DS18B20",
        false,
        false,
        false,
        true,
        true,
        5000U,
        0U,
        0x00U,
        0U,
        -1,
        -1,
        0U,
        &validateDs18b20Record,
        &createDs18b20Sensor,
    },
    {
        SensorType::kHtu2x,
        "htu2x",
        "HTU2X",
        true,
        false,
        false,
        false,
        true,
        5000U,
        0U,
        0x40U,
        0U,
        -1,
        -1,
        0U,
        &validateHtu2xRecord,
        &createHtu2xSensor,
    },
    {
        SensorType::kSht4x,
        "sht4x",
        "SHT4X",
        true,
        false,
        false,
        false,
        true,
        5000U,
        0U,
        0x44U,
        0U,
        -1,
        -1,
        0U,
        &validateSht4xRecord,
        &createSht4xSensor,
    },
    {
        SensorType::kMe3No2,
        "me3_no2",
        "ME3-NO2",
        false,
        true,
        false,
        false,
        true,
        5000U,
        0U,
        0x00U,
        0U,
        -1,
        -1,
        0U,
        &validateMe3No2Record,
        &createMe3No2Sensor,
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
