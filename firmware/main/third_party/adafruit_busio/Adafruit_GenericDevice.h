#pragma once

#include <cstddef>
#include <cstdint>

class Adafruit_GenericDevice {
  public:
    bool writeRegister(const std::uint8_t*, std::size_t, const std::uint8_t*, std::size_t) {
        return false;
    }

    bool readRegister(const std::uint8_t*, std::size_t, std::uint8_t*, std::size_t) {
        return false;
    }
};
