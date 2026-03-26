#pragma once

#include <cstddef>
#include <cstdint>

namespace air360 {

constexpr std::size_t kMaxConfiguredSensors = 8U;
constexpr std::size_t kSensorDisplayNameCapacity = 32U;

enum class SensorType : std::uint8_t {
    kUnknown = 0U,
    kBme280 = 1U,
    kGpsNmea = 2U,
    kDht11 = 3U,
    kDht22 = 4U,
    kBme680 = 5U,
};

enum class TransportKind : std::uint8_t {
    kUnknown = 0U,
    kI2c = 1U,
    kAnalog = 2U,
    kUart = 3U,
    kGpio = 4U,
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
        case TransportKind::kUart:
            return "uart";
        case TransportKind::kGpio:
            return "gpio";
        case TransportKind::kUnknown:
        default:
            return "unknown";
    }
}

constexpr std::size_t kMaxMeasurementValues = 8U;

enum class SensorValueKind : std::uint8_t {
    kUnknown = 0U,
    kTemperatureC = 1U,
    kHumidityPercent = 2U,
    kPressureHpa = 3U,
    kLatitudeDeg = 4U,
    kLongitudeDeg = 5U,
    kAltitudeM = 6U,
    kSatellites = 7U,
    kSpeedKnots = 8U,
    kGasResistanceOhms = 9U,
};

inline const char* sensorValueKindKey(SensorValueKind kind) {
    switch (kind) {
        case SensorValueKind::kTemperatureC:
            return "temperature_c";
        case SensorValueKind::kHumidityPercent:
            return "humidity_percent";
        case SensorValueKind::kPressureHpa:
            return "pressure_hpa";
        case SensorValueKind::kLatitudeDeg:
            return "latitude_deg";
        case SensorValueKind::kLongitudeDeg:
            return "longitude_deg";
        case SensorValueKind::kAltitudeM:
            return "altitude_m";
        case SensorValueKind::kSatellites:
            return "satellites";
        case SensorValueKind::kSpeedKnots:
            return "speed_knots";
        case SensorValueKind::kGasResistanceOhms:
            return "gas_resistance_ohms";
        case SensorValueKind::kUnknown:
        default:
            return "unknown";
    }
}

inline const char* sensorValueKindLabel(SensorValueKind kind) {
    switch (kind) {
        case SensorValueKind::kTemperatureC:
            return "Temperature";
        case SensorValueKind::kHumidityPercent:
            return "Humidity";
        case SensorValueKind::kPressureHpa:
            return "Pressure";
        case SensorValueKind::kLatitudeDeg:
            return "Latitude";
        case SensorValueKind::kLongitudeDeg:
            return "Longitude";
        case SensorValueKind::kAltitudeM:
            return "Altitude";
        case SensorValueKind::kSatellites:
            return "Satellites";
        case SensorValueKind::kSpeedKnots:
            return "Speed";
        case SensorValueKind::kGasResistanceOhms:
            return "Gas resistance";
        case SensorValueKind::kUnknown:
        default:
            return "Value";
    }
}

inline const char* sensorValueKindUnit(SensorValueKind kind) {
    switch (kind) {
        case SensorValueKind::kTemperatureC:
            return "C";
        case SensorValueKind::kHumidityPercent:
            return "%";
        case SensorValueKind::kPressureHpa:
            return "hPa";
        case SensorValueKind::kLatitudeDeg:
        case SensorValueKind::kLongitudeDeg:
            return "deg";
        case SensorValueKind::kAltitudeM:
            return "m";
        case SensorValueKind::kSatellites:
            return "";
        case SensorValueKind::kSpeedKnots:
            return "kn";
        case SensorValueKind::kGasResistanceOhms:
            return "Ohm";
        case SensorValueKind::kUnknown:
        default:
            return "";
    }
}

inline int sensorValueKindPrecision(SensorValueKind kind) {
    switch (kind) {
        case SensorValueKind::kTemperatureC:
        case SensorValueKind::kHumidityPercent:
        case SensorValueKind::kPressureHpa:
        case SensorValueKind::kAltitudeM:
        case SensorValueKind::kSpeedKnots:
            return 1;
        case SensorValueKind::kGasResistanceOhms:
            return 0;
        case SensorValueKind::kLatitudeDeg:
        case SensorValueKind::kLongitudeDeg:
            return 6;
        case SensorValueKind::kSatellites:
            return 0;
        case SensorValueKind::kUnknown:
        default:
            return 2;
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
