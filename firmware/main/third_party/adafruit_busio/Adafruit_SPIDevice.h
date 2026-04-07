#pragma once

#include <cstddef>
#include <cstdint>

class Adafruit_SPIDevice {
  public:
    bool write(const std::uint8_t*, std::size_t, const std::uint8_t*, std::size_t) {
        return false;
    }

    bool write_then_read(const std::uint8_t*, std::size_t, std::uint8_t*, std::size_t) {
        return false;
    }
};
