#include "air360/sensors/drivers/bme280_sensor.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

#include "air360/sensors/transport_binding.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.sensor.bme280";
// BME280 traffic is tiny, so 100 kHz keeps the shared bus conservative with no
// practical downside for multi-second environmental polling.
constexpr std::uint32_t kBme280I2cSpeedHz = 100000U;
// A forced T+P+H conversion at x1 oversampling finishes in under 10 ms; five
// 10 ms waits bound the poll at 50 ms even if the sensor never deasserts busy.
constexpr int kMeasureWaitAttempts = 5;
constexpr TickType_t kMeasureWaitTicks = pdMS_TO_TICKS(10);

}  // namespace

Bme280Sensor::~Bme280Sensor() {
    teardown();
}

SensorType Bme280Sensor::type() const {
    return SensorType::kBme280;
}

esp_err_t Bme280Sensor::init(
    const SensorRecord& record,
    const SensorDriverContext& context) {
    teardown();
    measurement_.clear();
    clearError();
    soft_fail_policy_.onPollOk();

    i2c_port_t port = I2C_NUM_0;
    gpio_num_t sda = GPIO_NUM_NC;
    gpio_num_t scl = GPIO_NUM_NC;
    if (!context.i2c_bus_manager->resolvePins(record.i2c_bus_id, port, sda, scl)) {
        setError("Unknown I2C bus id for BME280.");
        return ESP_ERR_NOT_SUPPORTED;
    }

    std::memset(&device_, 0, sizeof(device_));
    esp_err_t err = bmp280_init_desc(&device_, record.i2c_address, port, sda, scl);
    if (err != ESP_OK) {
        setError("Failed to initialize BME280 descriptor.");
        teardown();
        return err;
    }
    descriptor_initialized_ = true;
    context.i2c_bus_manager->applyDescriptorDefaults(device_.i2c_dev, kBme280I2cSpeedHz);

    // Forced mode with x1 oversampling and no filter mirrors the profile used
    // before the esp-idf-lib migration: each poll triggers a one-shot
    // conversion, which suits the multi-second polling model. Standby time is
    // irrelevant in forced mode.
    bmp280_params_t params{};
    params.mode = BMP280_MODE_FORCED;
    params.filter = BMP280_FILTER_OFF;
    params.oversampling_pressure = BMP280_ULTRA_LOW_POWER;
    params.oversampling_temperature = BMP280_ULTRA_LOW_POWER;
    params.oversampling_humidity = BMP280_ULTRA_LOW_POWER;
    params.standby = BMP280_STANDBY_05;

    // bmp280_init() reads and validates the chip ID internally.
    err = bmp280_init(&device_, &params);
    if (err != ESP_OK) {
        setError(std::string("Failed to initialize BME280 (chip ID mismatch or I2C error): ") +
                 esp_err_to_name(err));
        teardown();
        return err;
    }

    // The component also accepts the humidity-less BMP280; this sensor slot
    // expects the BME280 measurement set, so reject the wrong chip explicitly.
    if (device_.id != BME280_CHIP_ID) {
        setError("Detected a BMP280 (no humidity) where a BME280 was expected.");
        teardown();
        return ESP_ERR_NOT_SUPPORTED;
    }

    initialized_ = true;
    return ESP_OK;
}

esp_err_t Bme280Sensor::poll() {
    if (!initialized_ || !descriptor_initialized_) {
        setError("BME280 sensor is not initialized.");
        return ESP_ERR_INVALID_STATE;
    }

    if (esp_err_t err = bmp280_force_measurement(&device_); err != ESP_OK) {
        return reportPollFailure(
            kTag,
            std::string("Failed to start BME280 forced measurement: ") + esp_err_to_name(err),
            err);
    }

    bool busy = true;
    for (int attempt = 0; attempt < kMeasureWaitAttempts && busy; ++attempt) {
        vTaskDelay(kMeasureWaitTicks);
        if (esp_err_t err = bmp280_is_measuring(&device_, &busy); err != ESP_OK) {
            return reportPollFailure(
                kTag,
                std::string("Failed to read BME280 status: ") + esp_err_to_name(err),
                err);
        }
    }
    if (busy) {
        return reportPollFailure(
            kTag, "BME280 forced measurement did not finish in time.", ESP_ERR_TIMEOUT);
    }

    float temperature_c = 0.0F;
    float pressure_pa = 0.0F;
    float humidity_percent = 0.0F;
    if (esp_err_t err =
            bmp280_read_float(&device_, &temperature_c, &pressure_pa, &humidity_percent);
        err != ESP_OK) {
        return reportPollFailure(
            kTag,
            std::string("Failed to read BME280 measurement: ") + esp_err_to_name(err),
            err);
    }

    if (std::isnan(temperature_c) || std::isnan(pressure_pa) || std::isnan(humidity_percent)) {
        return reportPollFailure(kTag, "BME280 returned invalid values.", ESP_ERR_INVALID_RESPONSE);
    }

    measurement_.clear();
    measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
    measurement_.addValue(SensorValueKind::kTemperatureC, temperature_c);
    measurement_.addValue(SensorValueKind::kHumidityPercent, humidity_percent);
    measurement_.addValue(SensorValueKind::kPressureHpa, pressure_pa / kPaPerHpa);
    notePollSuccess();
    return ESP_OK;
}

SensorMeasurement Bme280Sensor::latestMeasurement() const {
    return measurement_;
}

void Bme280Sensor::teardown() {
    initialized_ = false;
    soft_fail_policy_.onPollOk();
    if (descriptor_initialized_) {
        if (esp_err_t err = bmp280_free_desc(&device_); err != ESP_OK) {
            ESP_LOGW(kTag, "Failed to free BME280 descriptor: %s", esp_err_to_name(err));
        }
        std::memset(&device_, 0, sizeof(device_));
        descriptor_initialized_ = false;
    }
}

std::unique_ptr<SensorDriver> createBme280Sensor() {
    return std::make_unique<Bme280Sensor>();
}

}  // namespace air360
