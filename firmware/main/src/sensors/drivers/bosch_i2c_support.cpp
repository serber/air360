#include "air360/sensors/drivers/bosch_i2c_support.hpp"

#include "air360/sensors/transport_binding.hpp"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace air360 {

esp_err_t boschI2cRead(
    const BoschI2cContext& context,
    std::uint8_t reg_addr,
    std::uint8_t* reg_data,
    std::uint32_t length) {
    if (context.bus_manager == nullptr || reg_data == nullptr || length == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    return context.bus_manager->readRegister(
        context.bus_id,
        context.address,
        reg_addr,
        reg_data,
        length);
}

esp_err_t boschI2cWrite(
    const BoschI2cContext& context,
    std::uint8_t reg_addr,
    const std::uint8_t* reg_data,
    std::uint32_t length) {
    if (context.bus_manager == nullptr || reg_data == nullptr || length == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    return context.bus_manager->write(
        context.bus_id,
        context.address,
        reg_addr,
        reg_data,
        length);
}

void boschDelayUs(std::uint32_t period_us) {
    if (period_us >= 1000U) {
        const TickType_t delay_ticks = pdMS_TO_TICKS((period_us + 999U) / 1000U);
        vTaskDelay(delay_ticks == 0 ? 1 : delay_ticks);
        return;
    }

    esp_rom_delay_us(period_us);
}

}  // namespace air360
