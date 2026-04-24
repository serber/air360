#pragma once

#include <cstdint>

namespace air360::ble {

inline void writeLe16(std::uint8_t* dst, std::uint16_t value) {
    dst[0] = static_cast<std::uint8_t>(value & 0xFFU);
    dst[1] = static_cast<std::uint8_t>(value >> 8U);
}

inline void writeLe24(std::uint8_t* dst, std::uint32_t value) {
    dst[0] = static_cast<std::uint8_t>(value & 0xFFU);
    dst[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    dst[2] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
}

}  // namespace air360::ble
