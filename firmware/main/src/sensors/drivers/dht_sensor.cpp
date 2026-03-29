#include "air360/sensors/drivers/dht_sensor.hpp"

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>

#include "DHT.h"
#include "esp_timer.h"

namespace air360 {

DhtSensor::DhtSensor(DhtModel model) : model_(model) {}

DhtSensor::~DhtSensor() = default;

SensorType DhtSensor::type() const {
    return model_ == DhtModel::kDht11 ? SensorType::kDht11 : SensorType::kDht22;
}

esp_err_t DhtSensor::init(const SensorRecord& record, const SensorDriverContext& context) {
    static_cast<void>(context);
    record_ = record;
    sensor_.reset();
    measurement_.clear();
    last_error_.clear();
    initialized_ = false;

    if (record_.analog_gpio_pin < 0) {
        setError("DHT GPIO pin is not configured.");
        return ESP_ERR_INVALID_ARG;
    }

    const std::uint8_t dht_type = model_ == DhtModel::kDht11 ? DHT11 : DHT22;
    sensor_ = std::make_unique<DHT>(
        static_cast<std::uint8_t>(record_.analog_gpio_pin),
        dht_type);
    if (sensor_ == nullptr) {
        setError("Failed to allocate DHT driver.");
        return ESP_ERR_NO_MEM;
    }

    sensor_->begin();
    initialized_ = true;
    return ESP_OK;
}

esp_err_t DhtSensor::poll() {
    if (!initialized_) {
        setError("DHT sensor is not initialized.");
        return ESP_ERR_INVALID_STATE;
    }

    if (sensor_ == nullptr) {
        setError("DHT driver is unavailable.");
        initialized_ = false;
        return ESP_ERR_INVALID_STATE;
    }

    if (!sensor_->read()) {
        setError("Failed to read DHT frame.");
        initialized_ = false;
        return ESP_ERR_INVALID_RESPONSE;
    }

    const float temperature_c = sensor_->readTemperature(false, false);
    const float humidity_percent = sensor_->readHumidity(false);
    if (std::isnan(temperature_c) || std::isnan(humidity_percent)) {
        setError("Adafruit DHT library returned invalid values.");
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
