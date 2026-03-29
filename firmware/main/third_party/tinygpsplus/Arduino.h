#pragma once

#include <chrono>
#include <cmath>
#include <cstdint>

using byte = std::uint8_t;

unsigned long millis();

#ifndef TWO_PI
#define TWO_PI (2.0 * M_PI)
#endif

template <typename T>
constexpr T sq(T value) {
    return value * value;
}

inline double radians(double degrees_value) {
    return degrees_value * (M_PI / 180.0);
}

inline double degrees(double radians_value) {
    return radians_value * (180.0 / M_PI);
}
