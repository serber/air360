#pragma once

#include <cstddef>
#include <cstdint>

namespace air360 {

constexpr std::size_t kMaxConfiguredSensors = 8U;
constexpr std::size_t kSensorDisplayNameCapacity = 32U;

enum class SensorType : std::uint8_t {
    kUnknown = 0U,
    kBme280 = 1U,
};

enum class TransportKind : std::uint8_t {
    kUnknown = 0U,
    kI2c = 1U,
    kAnalog = 2U,
};

enum class SensorRuntimeState : std::uint8_t {
    kDisabled = 0U,
    kConfigured = 1U,
    kInitialized = 2U,
    kPolling = 3U,
    kAbsent = 4U,
    kUnsupported = 5U,
    kError = 6U,
};

inline const char* transportKindKey(TransportKind kind) {
    switch (kind) {
        case TransportKind::kI2c:
            return "i2c";
        case TransportKind::kAnalog:
            return "analog";
        case TransportKind::kUnknown:
        default:
            return "unknown";
    }
}

inline const char* sensorRuntimeStateKey(SensorRuntimeState state) {
    switch (state) {
        case SensorRuntimeState::kDisabled:
            return "disabled";
        case SensorRuntimeState::kConfigured:
            return "configured";
        case SensorRuntimeState::kInitialized:
            return "initialized";
        case SensorRuntimeState::kPolling:
            return "polling";
        case SensorRuntimeState::kAbsent:
            return "absent";
        case SensorRuntimeState::kUnsupported:
            return "unsupported";
        case SensorRuntimeState::kError:
        default:
            return "error";
    }
}

}  // namespace air360
