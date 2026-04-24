#include "air360/sensors/drivers/dht_sensor.hpp"

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.sensor.dht";

dht_sensor_type_t toComponentType(DhtModel model) {
    return model == DhtModel::kDht11 ? DHT_TYPE_DHT11 : DHT_TYPE_AM2301;
}

}  // namespace

DhtSensor::DhtSensor(DhtModel model) : model_(model) {}

DhtSensor::~DhtSensor() = default;

SensorType DhtSensor::type() const {
    return model_ == DhtModel::kDht11 ? SensorType::kDht11 : SensorType::kDht22;
}

esp_err_t DhtSensor::init(const SensorRecord& record, const SensorDriverContext& context) {
    // DHT uses its configured GPIO directly and does not need the shared driver context.
    static_cast<void>(context);
    record_ = record;
    measurement_.clear();
    last_error_.clear();
    soft_fail_policy_.onPollOk();
    initialized_ = false;

    if (record_.analog_gpio_pin < 0) {
        setError("DHT GPIO pin is not configured.");
        return ESP_ERR_INVALID_ARG;
    }

    initialized_ = true;
    return ESP_OK;
}

esp_err_t DhtSensor::poll() {
    if (!initialized_) {
        setError("DHT sensor is not initialized.");
        return ESP_ERR_INVALID_STATE;
    }

    float humidity_percent = NAN;
    float temperature_c = NAN;
    const esp_err_t err = dht_read_float_data(
        toComponentType(model_),
        static_cast<gpio_num_t>(record_.analog_gpio_pin),
        &humidity_percent,
        &temperature_c);
    if (err != ESP_OK) {
        setError(std::string("Failed to read DHT sample: ") + esp_err_to_name(err) + ".");
        if (soft_fail_policy_.onPollErr()) {
            ESP_LOGE(kTag, "hard error after %u soft fails: %s", kSensorPollFailureReinitThreshold, last_error_.c_str());
            initialized_ = false;
        } else if (soft_fail_policy_.soft_fails == 1U) {
            ESP_LOGW(kTag, "soft fail 1/%u: %s", kSensorPollFailureReinitThreshold, last_error_.c_str());
        }
        return err;
    }

    if (std::isnan(temperature_c) || std::isnan(humidity_percent)) {
        setError("DHT component returned invalid values.");
        if (soft_fail_policy_.onPollErr()) {
            ESP_LOGE(kTag, "hard error after %u soft fails: %s", kSensorPollFailureReinitThreshold, last_error_.c_str());
            initialized_ = false;
        } else if (soft_fail_policy_.soft_fails == 1U) {
            ESP_LOGW(kTag, "soft fail 1/%u: %s", kSensorPollFailureReinitThreshold, last_error_.c_str());
        }
        return ESP_ERR_INVALID_RESPONSE;
    }

    measurement_.clear();
    measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
    measurement_.addValue(SensorValueKind::kTemperatureC, temperature_c);
    measurement_.addValue(SensorValueKind::kHumidityPercent, humidity_percent);
    soft_fail_policy_.onPollOk();
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
