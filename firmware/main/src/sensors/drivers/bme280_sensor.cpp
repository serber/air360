#include "air360/sensors/drivers/bme280_sensor.hpp"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

#include "air360/sensors/transport_binding.hpp"
#include "esp_log.h"
#include "esp_timer.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.sensor.bme280";
constexpr bme280_sensor_sampling kOversampling = BME280_SAMPLING_X1;
constexpr bme280_sensor_filter kFilter = BME280_FILTER_OFF;
constexpr bme280_standby_duration kStandbyDuration = BME280_STANDBY_MS_0_5;
// BME280 traffic is tiny, so 100 kHz keeps the shared bus conservative with no
// practical downside for multi-second environmental polling.
constexpr std::uint32_t kBme280I2cSpeedHz = 100000U;

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
    last_error_.clear();
    soft_fail_policy_.onPollOk();

    esp_err_t err = context.i2c_bus_manager->setupDevice(record, kBme280I2cSpeedHz, dev_);
    if (err != ESP_OK) {
        setError(std::string("Failed to set up I2C device for BME280: ") + esp_err_to_name(err));
        teardown();
        return err;
    }
    dev_initialized_ = true;

    err = i2c_dev_check_present(&dev_);
    if (err != ESP_OK) {
        setError("BME280 did not respond on the selected I2C bus and address.");
        teardown();
        return ESP_ERR_NOT_FOUND;
    }

    err = context.i2c_bus_manager->getComponentBus(record.i2c_bus_id, bus_);
    if (err != ESP_OK) {
        setError("Failed to bind BME280 to the shared I2C bus.");
        teardown();
        return err;
    }

    sensor_ = bme280_create(bus_, record.i2c_address);
    if (sensor_ == nullptr) {
        setError("Failed to allocate the BME280 component driver.");
        teardown();
        return ESP_ERR_NO_MEM;
    }

    // bme280_default_init() reads and validates the chip ID internally.
    err = bme280_default_init(sensor_);
    if (err != ESP_OK) {
        setError("Failed to initialize BME280 (chip ID mismatch or I2C error).");
        teardown();
        return err;
    }

    err = configureSensor();
    if (err != ESP_OK) {
        teardown();
        return err;
    }

    initialized_ = true;
    return ESP_OK;
}

esp_err_t Bme280Sensor::poll() {
    if (!initialized_) {
        setError("BME280 is not initialized.");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = bme280_take_forced_measurement(sensor_);
    if (err != ESP_OK) {
        setError("Failed to start BME280 forced measurement.");
        if (soft_fail_policy_.onPollErr()) {
            ESP_LOGE(kTag, "hard error after %u soft fails: %s", kSensorPollFailureReinitThreshold, last_error_.c_str());
            initialized_ = false;
        } else if (soft_fail_policy_.soft_fails == 1U) {
            ESP_LOGW(kTag, "soft fail 1/%u: %s", kSensorPollFailureReinitThreshold, last_error_.c_str());
        }
        return err;
    }

    float temperature = 0.0F;
    err = bme280_read_temperature(sensor_, &temperature);
    if (err != ESP_OK) {
        setError("Failed to read BME280 temperature.");
        if (soft_fail_policy_.onPollErr()) {
            ESP_LOGE(kTag, "hard error after %u soft fails: %s", kSensorPollFailureReinitThreshold, last_error_.c_str());
            initialized_ = false;
        } else if (soft_fail_policy_.soft_fails == 1U) {
            ESP_LOGW(kTag, "soft fail 1/%u: %s", kSensorPollFailureReinitThreshold, last_error_.c_str());
        }
        return err;
    }

    float humidity = 0.0F;
    err = bme280_read_humidity(sensor_, &humidity);
    if (err != ESP_OK) {
        setError("Failed to read BME280 humidity.");
        if (soft_fail_policy_.onPollErr()) {
            ESP_LOGE(kTag, "hard error after %u soft fails: %s", kSensorPollFailureReinitThreshold, last_error_.c_str());
            initialized_ = false;
        } else if (soft_fail_policy_.soft_fails == 1U) {
            ESP_LOGW(kTag, "soft fail 1/%u: %s", kSensorPollFailureReinitThreshold, last_error_.c_str());
        }
        return err;
    }

    float pressure_hpa = 0.0F;
    err = bme280_read_pressure(sensor_, &pressure_hpa);
    if (err != ESP_OK) {
        setError("Failed to read BME280 pressure.");
        if (soft_fail_policy_.onPollErr()) {
            ESP_LOGE(kTag, "hard error after %u soft fails: %s", kSensorPollFailureReinitThreshold, last_error_.c_str());
            initialized_ = false;
        } else if (soft_fail_policy_.soft_fails == 1U) {
            ESP_LOGW(kTag, "soft fail 1/%u: %s", kSensorPollFailureReinitThreshold, last_error_.c_str());
        }
        return err;
    }

    measurement_.clear();
    measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
    measurement_.addValue(SensorValueKind::kTemperatureC, temperature);
    measurement_.addValue(SensorValueKind::kHumidityPercent, humidity);
    measurement_.addValue(SensorValueKind::kPressureHpa, pressure_hpa);
    soft_fail_policy_.onPollOk();
    last_error_.clear();
    return ESP_OK;
}

SensorMeasurement Bme280Sensor::latestMeasurement() const {
    return measurement_;
}

std::string Bme280Sensor::lastError() const {
    return last_error_;
}

esp_err_t Bme280Sensor::configureSensor() {
    const esp_err_t err = bme280_set_sampling(
        sensor_,
        BME280_MODE_FORCED,
        kOversampling,
        kOversampling,
        kOversampling,
        kFilter,
        kStandbyDuration);
    if (err != ESP_OK) {
        setError("Failed to configure BME280 settings.");
        return err;
    }

    return ESP_OK;
}

void Bme280Sensor::teardown() {
    if (sensor_ != nullptr) {
        bme280_delete(&sensor_);
        sensor_ = nullptr;
    }

    // i2c_bus_create() borrowed the bus from i2cdev — do not delete the bus itself.

    if (dev_initialized_) {
        i2c_dev_delete_mutex(&dev_);
    }

    std::memset(&dev_, 0, sizeof(dev_));
    dev_initialized_ = false;
    bus_ = nullptr;
    initialized_ = false;
    soft_fail_policy_.onPollOk();
}

void Bme280Sensor::setError(const std::string& message) {
    last_error_ = message;
}

std::unique_ptr<SensorDriver> createBme280Sensor() {
    return std::make_unique<Bme280Sensor>();
}

}  // namespace air360
