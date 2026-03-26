#include "sensirion_i2c_hal.h"

#include <cstdint>

#include "air360/sensors/drivers/sps30_i2c_support.hpp"
#include "air360/sensors/transport_binding.hpp"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sensirion_common.h"
#include "sensirion_i2c.h"

namespace {

air360::I2cBusManager* g_bus_manager = nullptr;
std::uint8_t g_bus_id = 0U;

}  // namespace

namespace air360 {

void sps30HalSetContext(I2cBusManager* bus_manager, std::uint8_t bus_id) {
    g_bus_manager = bus_manager;
    g_bus_id = bus_id;
}

void sps30HalClearContext() {
    g_bus_manager = nullptr;
    g_bus_id = 0U;
}

}  // namespace air360

extern "C" {

int16_t sensirion_i2c_hal_select_bus(uint8_t bus_idx) {
    g_bus_id = bus_idx;
    return NO_ERROR;
}

void sensirion_i2c_hal_init(void) {}

void sensirion_i2c_hal_free(void) {}

int8_t sensirion_i2c_hal_read(uint8_t address, uint8_t* data, uint8_t count) {
    if (g_bus_manager == nullptr || data == nullptr || count == 0U) {
        return I2C_BUS_ERROR;
    }

    return g_bus_manager->readRaw(g_bus_id, address, data, count) == ESP_OK ? NO_ERROR
                                                                             : I2C_BUS_ERROR;
}

int8_t sensirion_i2c_hal_write(uint8_t address, const uint8_t* data, uint8_t count) {
    if (g_bus_manager == nullptr || data == nullptr || count == 0U) {
        return I2C_BUS_ERROR;
    }

    return g_bus_manager->writeRaw(g_bus_id, address, data, count) == ESP_OK ? NO_ERROR
                                                                              : I2C_BUS_ERROR;
}

void sensirion_i2c_hal_sleep_usec(uint32_t useconds) {
    if (useconds >= 1000U) {
        const TickType_t delay_ticks = pdMS_TO_TICKS((useconds + 999U) / 1000U);
        vTaskDelay(delay_ticks == 0 ? 1 : delay_ticks);
        return;
    }

    esp_rom_delay_us(useconds);
}

}  // extern "C"
