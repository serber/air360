#include "air360/sensors/drivers/dht_sensor.hpp"

#include <cstdint>
#include <memory>
#include <string>

#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace air360 {

namespace {

constexpr std::int64_t kResponseTimeoutUs = 120;
constexpr std::int64_t kBitHighThresholdUs = 50;
static portMUX_TYPE g_dht_mux = portMUX_INITIALIZER_UNLOCKED;

bool waitForLevel(gpio_num_t pin, int level, std::int64_t timeout_us) {
    const std::int64_t start_us = esp_timer_get_time();
    while ((esp_timer_get_time() - start_us) < timeout_us) {
        if (gpio_get_level(pin) == level) {
            return true;
        }
    }
    return false;
}

bool measureHighPulseUs(gpio_num_t pin, std::int64_t& high_us) {
    if (!waitForLevel(pin, 1, kResponseTimeoutUs)) {
        return false;
    }

    const std::int64_t start_us = esp_timer_get_time();
    if (!waitForLevel(pin, 0, kResponseTimeoutUs)) {
        return false;
    }
    high_us = esp_timer_get_time() - start_us;
    return true;
}

}  // namespace

DhtSensor::DhtSensor(DhtModel model) : model_(model) {}

SensorType DhtSensor::type() const {
    return model_ == DhtModel::kDht11 ? SensorType::kDht11 : SensorType::kDht22;
}

esp_err_t DhtSensor::init(const SensorRecord& record, const SensorDriverContext& context) {
    static_cast<void>(context);
    record_ = record;
    measurement_.clear();
    last_error_.clear();
    initialized_ = false;

    if (record_.analog_gpio_pin < 0) {
        setError("DHT GPIO pin is not configured.");
        return ESP_ERR_INVALID_ARG;
    }

    const gpio_num_t pin = static_cast<gpio_num_t>(record_.analog_gpio_pin);
    gpio_config_t config{};
    config.pin_bit_mask = 1ULL << static_cast<unsigned>(pin);
    config.mode = GPIO_MODE_INPUT_OUTPUT_OD;
    config.pull_up_en = GPIO_PULLUP_ENABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    const esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) {
        setError("Failed to configure DHT GPIO.");
        return err;
    }

    gpio_set_level(pin, 1);
    initialized_ = true;
    return ESP_OK;
}

esp_err_t DhtSensor::poll() {
    if (!initialized_) {
        setError("DHT sensor is not initialized.");
        return ESP_ERR_INVALID_STATE;
    }

    std::uint8_t raw[5]{};
    const esp_err_t err = readRawFrame(raw);
    if (err != ESP_OK) {
        initialized_ = false;
        return err;
    }

    float temperature_c = 0.0F;
    float humidity_percent = 0.0F;
    if (!decodeFrame(raw, temperature_c, humidity_percent)) {
        setError("Failed to decode DHT data frame.");
        initialized_ = false;
        return ESP_ERR_INVALID_RESPONSE;
    }

    measurement_.clear();
    measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
    measurement_.addValue(SensorValueKind::kTemperatureC, temperature_c);
    measurement_.addValue(SensorValueKind::kHumidityPercent, humidity_percent);
    last_error_.clear();
    return ESP_OK;
}

SensorMeasurement DhtSensor::latestMeasurement() const {
    return measurement_;
}

std::string DhtSensor::lastError() const {
    return last_error_;
}

esp_err_t DhtSensor::readRawFrame(std::uint8_t data[5]) {
    const gpio_num_t pin = static_cast<gpio_num_t>(record_.analog_gpio_pin);
    const std::uint32_t start_low_us = model_ == DhtModel::kDht11 ? 20000U : 1200U;

    taskENTER_CRITICAL(&g_dht_mux);

    gpio_set_direction(pin, GPIO_MODE_OUTPUT_OD);
    gpio_set_level(pin, 0);
    esp_rom_delay_us(start_low_us);
    gpio_set_level(pin, 1);
    esp_rom_delay_us(30);
    gpio_set_direction(pin, GPIO_MODE_INPUT);

    if (!waitForLevel(pin, 0, kResponseTimeoutUs) ||
        !waitForLevel(pin, 1, kResponseTimeoutUs) ||
        !waitForLevel(pin, 0, kResponseTimeoutUs)) {
        taskEXIT_CRITICAL(&g_dht_mux);
        setError("DHT did not acknowledge start signal.");
        return ESP_ERR_TIMEOUT;
    }

    for (int bit_index = 0; bit_index < 40; ++bit_index) {
        std::int64_t high_us = 0;
        if (!measureHighPulseUs(pin, high_us)) {
            taskEXIT_CRITICAL(&g_dht_mux);
            setError("Timed out while reading DHT bit stream.");
            return ESP_ERR_TIMEOUT;
        }

        data[bit_index / 8] <<= 1;
        if (high_us > kBitHighThresholdUs) {
            data[bit_index / 8] |= 0x01U;
        }
    }

    taskEXIT_CRITICAL(&g_dht_mux);
    return ESP_OK;
}

bool DhtSensor::decodeFrame(
    const std::uint8_t data[5],
    float& temperature_c,
    float& humidity_percent) const {
    const std::uint8_t checksum =
        static_cast<std::uint8_t>(data[0] + data[1] + data[2] + data[3]);
    if (checksum != data[4]) {
        return false;
    }

    if (model_ == DhtModel::kDht11) {
        humidity_percent = static_cast<float>(data[0]);
        temperature_c = static_cast<float>(data[2]);
        return true;
    }

    const std::uint16_t raw_humidity =
        static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[0]) << 8U) | data[1]);
    std::uint16_t raw_temperature =
        static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[2]) << 8U) | data[3]);

    humidity_percent = static_cast<float>(raw_humidity) / 10.0F;
    bool negative = (raw_temperature & 0x8000U) != 0U;
    raw_temperature &= 0x7FFFU;
    temperature_c = static_cast<float>(raw_temperature) / 10.0F;
    if (negative) {
        temperature_c = -temperature_c;
    }
    return true;
}

void DhtSensor::setError(const std::string& message) {
    last_error_ = message;
}

std::unique_ptr<SensorDriver> createDht11Sensor() {
    return std::make_unique<DhtSensor>(DhtModel::kDht11);
}

std::unique_ptr<SensorDriver> createDht22Sensor() {
    return std::make_unique<DhtSensor>(DhtModel::kDht22);
}

}  // namespace air360
