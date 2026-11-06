#pragma once

#include <cstdint>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string_view>

namespace air360::ble {

// Reserve flags and the service-data AD header before packing BTHome values.
constexpr std::size_t kAdvertisementCapacity = 31U;
constexpr std::size_t kServiceDataCapacity = kAdvertisementCapacity - 5U;

inline std::size_t buildAdvertisement(
    std::uint8_t* out, std::size_t capacity, std::string_view name,
    const std::uint8_t* service_data, std::size_t service_size) {
    capacity = std::min(capacity, kAdvertisementCapacity);
    if (out == nullptr || service_data == nullptr ||
        service_size > kServiceDataCapacity || capacity < service_size + 5U) {
        return 0U;
    }
    out[0] = 2U;
    out[1] = 0x01U;  // Flags AD type.
    out[2] = 0x06U;  // General discoverable, BR/EDR unsupported.
    out[3] = static_cast<std::uint8_t>(service_size + 1U);
    out[4] = 0x16U;  // Service data with 16-bit UUID.
    std::memcpy(out + 5U, service_data, service_size);
    std::size_t size = service_size + 5U;
    if (!name.empty() && capacity - size > 2U) {
        const std::size_t name_size = std::min(name.size(), capacity - size - 2U);
        out[size++] = static_cast<std::uint8_t>(name_size + 1U);
        out[size++] = name_size == name.size() ? 0x09U : 0x08U;
        std::memcpy(out + size, name.data(), name_size);
        size += name_size;
    }
    return size;
}

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
