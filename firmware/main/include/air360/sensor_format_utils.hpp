#pragma once

#include <cstdio>
#include <string>

#include "air360/sensors/sensor_types.hpp"

namespace air360 {

inline std::string formatSensorValue(SensorValueKind kind, float value) {
    char buffer[48];
    std::snprintf(
        buffer,
        sizeof(buffer),
        "%.*f",
        sensorValueKindPrecision(kind),
        static_cast<double>(value));
    return buffer;
}

}  // namespace air360
