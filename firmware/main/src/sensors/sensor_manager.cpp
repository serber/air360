#include "air360/sensors/sensor_manager.hpp"

#include <cstdio>
#include <string>
#include <utility>

#include "air360/sensors/sensor_registry.hpp"

namespace air360 {

namespace {

std::string defaultDisplayName(const SensorDescriptor* descriptor, std::uint32_t id) {
    if (descriptor != nullptr && descriptor->display_name != nullptr) {
        return std::string(descriptor->display_name) + " #" + std::to_string(id);
    }

    return std::string("Sensor #") + std::to_string(id);
}

std::string bindingSummary(const SensorRecord& record) {
    char buffer[64];
    switch (record.transport_kind) {
        case TransportKind::kI2c:
            std::snprintf(
                buffer,
                sizeof(buffer),
                "i2c%u @ 0x%02x",
                static_cast<unsigned>(record.i2c_bus_id),
                static_cast<unsigned>(record.i2c_address));
            return buffer;
        case TransportKind::kAnalog:
            std::snprintf(
                buffer,
                sizeof(buffer),
                "GPIO %d",
                static_cast<int>(record.analog_gpio_pin));
            return buffer;
        case TransportKind::kUnknown:
        default:
            return "unbound";
    }
}

}  // namespace

void SensorManager::applyConfig(const SensorConfigList& config) {
    sensors_.clear();
    sensors_.reserve(config.sensor_count);

    SensorRegistry registry;
    for (std::size_t index = 0; index < config.sensor_count; ++index) {
        const SensorRecord& record = config.sensors[index];
        const SensorDescriptor* descriptor = registry.findByType(record.sensor_type);

        SensorRuntimeInfo info;
        info.id = record.id;
        info.enabled = record.enabled != 0U;
        info.sensor_type = record.sensor_type;
        info.transport_kind = record.transport_kind;
        info.type_key =
            descriptor != nullptr ? descriptor->type_key : std::string("unknown");
        info.type_name =
            descriptor != nullptr ? descriptor->display_name : std::string("Unknown sensor");
        info.display_name =
            record.display_name[0] != '\0' ? std::string(record.display_name)
                                           : defaultDisplayName(descriptor, record.id);
        info.binding_summary = bindingSummary(record);

        if (!info.enabled) {
            info.state = SensorRuntimeState::kDisabled;
        } else if (descriptor == nullptr) {
            info.state = SensorRuntimeState::kUnsupported;
            info.last_error = "Unsupported sensor type";
        } else if (!registry.supportsTransport(*descriptor, record.transport_kind)) {
            info.state = SensorRuntimeState::kUnsupported;
            info.last_error = "Unsupported transport for selected sensor";
        } else if (!descriptor->driver_implemented) {
            info.state = SensorRuntimeState::kConfigured;
        } else {
            info.state = SensorRuntimeState::kInitialized;
        }

        sensors_.push_back(std::move(info));
    }
}

const std::vector<SensorRuntimeInfo>& SensorManager::sensors() const {
    return sensors_;
}

std::size_t SensorManager::configuredCount() const {
    return sensors_.size();
}

std::size_t SensorManager::enabledCount() const {
    std::size_t count = 0U;
    for (const auto& sensor : sensors_) {
        if (sensor.enabled) {
            ++count;
        }
    }
    return count;
}

}  // namespace air360
