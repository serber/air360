#include "sensirion_i2c_hal.h"

#include <cstdint>

#include "air360/sensors/drivers/sps30_i2c_support.hpp"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2cdev.h"
#include "sensirion_common.h"
#include "sensirion_i2c.h"

namespace {

i2c_dev_t* g_device = nullptr;

}  // namespace

namespace air360 {

void sps30HalSetContext(i2c_dev_t* device) {
    g_device = device;
}

void sps30HalClearContext() {
    g_device = nullptr;
}

}  // namespace air360

extern "C" {

int16_t sensirion_i2c_hal_select_bus(uint8_t bus_idx) {
    static_cast<void>(bus_idx);
    return NO_ERROR;
}

void sensirion_i2c_hal_init(void) {}

void sensirion_i2c_hal_free(void) {}

int8_t sensirion_i2c_hal_read(uint8_t address, uint8_t* data, uint8_t count) {
    static_cast<void>(address);
    if (g_device == nullptr || data == nullptr || count == 0U) {
        return I2C_BUS_ERROR;
    }

    return i2c_dev_read(g_device, nullptr, 0, data, count) == ESP_OK ? NO_ERROR : I2C_BUS_ERROR;
}

int8_t sensirion_i2c_hal_write(uint8_t address, const uint8_t* data, uint8_t count) {
    static_cast<void>(address);
    if (g_device == nullptr || data == nullptr || count == 0U) {
        return I2C_BUS_ERROR;
    }

    return i2c_dev_write(g_device, nullptr, 0, data, count) == ESP_OK ? NO_ERROR : I2C_BUS_ERROR;
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
