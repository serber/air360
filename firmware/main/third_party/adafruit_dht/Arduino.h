#pragma once

#include <cmath>
#include <cstdint>

#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

using byte = std::uint8_t;
using word = std::uint16_t;

#ifndef F_CPU
#define F_CPU (CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ * 1000000L)
#endif

#ifndef HIGH
#define HIGH 0x1
#endif

#ifndef LOW
#define LOW 0x0
#endif

#ifndef INPUT
#define INPUT 0x0
#endif

#ifndef OUTPUT
#define OUTPUT 0x1
#endif

#ifndef INPUT_PULLUP
#define INPUT_PULLUP 0x2
#endif

#define microsecondsToClockCycles(a) ((a) * (F_CPU / 1000000L))

inline unsigned long millis() {
    return static_cast<unsigned long>(esp_timer_get_time() / 1000ULL);
}

inline void delay(unsigned long ms) {
    const TickType_t ticks = pdMS_TO_TICKS(ms);
    vTaskDelay(ticks > 0 ? ticks : 1);
}

inline void delayMicroseconds(unsigned int us) {
    esp_rom_delay_us(us);
}

inline void pinMode(std::uint8_t pin, std::uint8_t mode) {
    gpio_config_t config{};
    config.pin_bit_mask = 1ULL << static_cast<unsigned>(pin);
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;

    switch (mode) {
        case OUTPUT:
            config.mode = GPIO_MODE_OUTPUT;
            config.pull_up_en = GPIO_PULLUP_DISABLE;
            break;
        case INPUT_PULLUP:
            config.mode = GPIO_MODE_INPUT;
            config.pull_up_en = GPIO_PULLUP_ENABLE;
            break;
        case INPUT:
        default:
            config.mode = GPIO_MODE_INPUT;
            config.pull_up_en = GPIO_PULLUP_DISABLE;
            break;
    }

    gpio_config(&config);
}

inline void digitalWrite(std::uint8_t pin, std::uint8_t value) {
    gpio_set_level(static_cast<gpio_num_t>(pin), value == HIGH ? 1 : 0);
}

inline int digitalRead(std::uint8_t pin) {
    return gpio_get_level(static_cast<gpio_num_t>(pin));
}

inline void noInterrupts() {
    portDISABLE_INTERRUPTS();
}

inline void interrupts() {
    portENABLE_INTERRUPTS();
}

template <typename T>
constexpr T min(T a, T b) {
    return a < b ? a : b;
}

template <typename T>
constexpr T max(T a, T b) {
    return a > b ? a : b;
}
