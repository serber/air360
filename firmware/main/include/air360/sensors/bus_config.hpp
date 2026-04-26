#pragma once

#include <cstdint>

#include "driver/gpio.h"

namespace air360 {

struct BusConfig {
    std::uint8_t  id;
    gpio_num_t    sda;
    gpio_num_t    scl;
    std::uint32_t clock_hz;
};

// Symbolic bus ID used in sensor descriptors so that the literal 0
// never appears in descriptor tables.
constexpr std::uint8_t kPrimaryI2cBus = 0U;

}  // namespace air360
