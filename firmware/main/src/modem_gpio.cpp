#include "air360/modem_gpio.hpp"

#include "air360/cellular_config_repository.hpp"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.modem_gpio";
// 0xFF is the persisted sentinel for “GPIO not wired” across cellular config.
constexpr std::uint8_t kNotWired = 0xFFU;

void configureOutputPin(std::uint8_t gpio_u8, const char* label) {
    if (gpio_u8 == kNotWired) {
        return;
    }
    const auto gpio = static_cast<gpio_num_t>(gpio_u8);
    gpio_config_t cfg{};
    cfg.pin_bit_mask = 1ULL << static_cast<unsigned>(gpio);
    cfg.mode = GPIO_MODE_OUTPUT;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    const esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "%s GPIO %u config failed: %s", label, gpio_u8, esp_err_to_name(err));
        return;
    }
    gpio_set_level(gpio, 0);
    ESP_LOGI(kTag, "%s → GPIO %u (output, idle LOW)", label, gpio_u8);
}

}  // namespace

void initModemGpios(const CellularConfig& config) {
    configureOutputPin(config.pwrkey_gpio, "PWRKEY");
    configureOutputPin(config.sleep_gpio,  "SLEEP");
    configureOutputPin(config.reset_gpio,  "RESET");
}

bool pulseModemPwrkey(std::uint8_t gpio_u8, std::uint32_t duration_ms) {
    if (gpio_u8 == kNotWired) {
        return false;
    }
    const auto gpio = static_cast<gpio_num_t>(gpio_u8);
    gpio_set_level(gpio, 1);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    gpio_set_level(gpio, 0);
    return true;
}

void setModemSleepPin(std::uint8_t gpio_u8, bool assert_sleep) {
    if (gpio_u8 == kNotWired) {
        return;
    }
    gpio_set_level(static_cast<gpio_num_t>(gpio_u8), assert_sleep ? 1 : 0);
}

}  // namespace air360
