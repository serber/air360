#pragma once

#include <cstdint>

#include "esp_err.h"

namespace air360 {

class I2cBusManager;

struct BoschI2cContext {
    I2cBusManager* bus_manager = nullptr;
    std::uint8_t bus_id = 0U;
    std::uint8_t address = 0U;
};

esp_err_t boschI2cRead(
    const BoschI2cContext& context,
    std::uint8_t reg_addr,
    std::uint8_t* reg_data,
    std::uint32_t length);

esp_err_t boschI2cWrite(
    const BoschI2cContext& context,
    std::uint8_t reg_addr,
    const std::uint8_t* reg_data,
    std::uint32_t length);

void boschDelayUs(std::uint32_t period_us);

}  // namespace air360
