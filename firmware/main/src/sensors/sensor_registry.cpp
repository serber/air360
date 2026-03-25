#include "air360/sensors/sensor_registry.hpp"

#include <cstring>
#include <string>

#include "air360/sensors/drivers/bme280_sensor.hpp"

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

constexpr SensorDescriptor kDescriptors[] = {
    {
        SensorType::kBme280,
        "bme280",
        "BME280",
        true,
        false,
        true,
        10000U,
        0U,
        0x76U,
        &validateBme280Record,
        &createBme280Sensor,
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
