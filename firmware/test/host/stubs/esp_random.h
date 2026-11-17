#pragma once

#include <cstddef>
#include <cstdint>

// Deterministic host stand-in for the hardware RNG.
namespace esp_random_test {
inline std::uint32_t state = 0x12345678U;
}

inline std::uint32_t esp_random() {
    esp_random_test::state = esp_random_test::state * 1664525U + 1013904223U;
    return esp_random_test::state;
}

inline void esp_fill_random(void* buffer, std::size_t size) {
    auto* bytes = static_cast<std::uint8_t*>(buffer);
    for (std::size_t index = 0U; index < size; ++index) {
        bytes[index] = static_cast<std::uint8_t>(esp_random() >> 24U);
    }
}
