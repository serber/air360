#include "air360/sensors/drivers/veml7700_sensor.hpp"

#include <memory>
#include <string>

#include "air360/sensors/transport_binding.hpp"
#include "Adafruit_VEML7700.h"
#include "Wire.h"
#include "esp_timer.h"

namespace air360 {

SensorType Veml7700Sensor::type() const {
    return SensorType::kVeml7700;
}

esp_err_t Veml7700Sensor::init(
    const SensorRecord& record,
    const SensorDriverContext& context) {
    record_ = record;
    i2c_bus_manager_ = context.i2c_bus_manager;
    measurement_.clear();
    last_error_.clear();
    sensor_.reset();
    wire_.reset();
    initialized_ = false;

    if (i2c_bus_manager_ == nullptr) {
        setError("I2C bus manager is unavailable.");
        return ESP_ERR_INVALID_STATE;
    }

    wire_ = std::make_unique<::TwoWire>();
    wire_->attach(i2c_bus_manager_, record_.i2c_bus_id);

    sensor_ = std::make_unique<::Adafruit_VEML7700>();
    if (!sensor_->begin(wire_.get())) {
        sensor_.reset();
        wire_.reset();
        setError("Failed to initialize Adafruit VEML7700 driver.");
        return ESP_ERR_NOT_FOUND;
    }

    initialized_ = true;
    return ESP_OK;
}

esp_err_t Veml7700Sensor::poll() {
    if (!initialized_ || sensor_ == nullptr) {
        setError("VEML7700 is not initialized.");
        return ESP_ERR_INVALID_STATE;
    }

    measurement_.clear();
    measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
    measurement_.addValue(
        SensorValueKind::kIlluminanceLux,
        sensor_->readLux(VEML_LUX_AUTO));
    last_error_.clear();
    return ESP_OK;
}

SensorMeasurement Veml7700Sensor::latestMeasurement() const {
    return measurement_;
}

std::string Veml7700Sensor::lastError() const {
    return last_error_;
}

void Veml7700Sensor::setError(const std::string& message) {
    last_error_ = message;
}

std::unique_ptr<SensorDriver> createVeml7700Sensor() {
    return std::make_unique<Veml7700Sensor>();
}

}  // namespace air360
