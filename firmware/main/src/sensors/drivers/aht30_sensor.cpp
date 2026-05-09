#include "air360/sensors/drivers/aht30_sensor.hpp"

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>

#include "air360/sensors/transport_binding.hpp"
#include "aht30.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.sensor.aht30";

}  // namespace

Aht30Sensor::~Aht30Sensor() {
    teardown();
}

SensorType Aht30Sensor::type() const {
    return SensorType::kAht30;
}

esp_err_t Aht30Sensor::init(const SensorRecord& record, const SensorDriverContext& context) {
    teardown();
    record_ = record;
    measurement_.clear();
    clearError();
    soft_fail_policy_.onPollOk();

    i2c_master_bus_handle_t bus_handle = nullptr;
    esp_err_t err = context.i2c_bus_manager->getMasterBusHandle(record.i2c_bus_id, bus_handle);
    if (err != ESP_OK) {
        setError(std::string("Failed to get I2C master bus handle for AHT30: ") + esp_err_to_name(err));
        return err;
    }

    err = aht30_create(bus_handle, record.i2c_address, &handle_);
    if (err != ESP_OK) {
        setError(std::string("Failed to create AHT30 sensor: ") + esp_err_to_name(err));
        handle_ = nullptr;
        return err;
    }

    initialized_ = true;
    return ESP_OK;
}

esp_err_t Aht30Sensor::poll() {
    if (!initialized_ || handle_ == nullptr) {
        setError("AHT30 sensor is not initialized.");
        return ESP_ERR_INVALID_STATE;
    }

    float temperature_c = 0.0F;
    float humidity_percent = 0.0F;
    if (esp_err_t err = aht30_get_temperature_humidity_value(handle_, &temperature_c, &humidity_percent);
        err != ESP_OK) {
        return reportPollFailure(
            kTag,
            std::string("Failed to read AHT30 measurement: ") + esp_err_to_name(err),
            err);
    }

    if (std::isnan(temperature_c) || std::isnan(humidity_percent)) {
        return reportPollFailure(kTag, "AHT30 returned invalid values.", ESP_ERR_INVALID_RESPONSE);
    }

    measurement_.clear();
    measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
    measurement_.addValue(SensorValueKind::kTemperatureC, temperature_c);
    measurement_.addValue(SensorValueKind::kHumidityPercent, humidity_percent);
    notePollSuccess();
    return ESP_OK;
}

SensorMeasurement Aht30Sensor::latestMeasurement() const {
    return measurement_;
}

void Aht30Sensor::teardown() {
    initialized_ = false;
    soft_fail_policy_.onPollOk();
    if (handle_ != nullptr) {
        aht30_delete(handle_);
        handle_ = nullptr;
    }
}

std::unique_ptr<SensorDriver> createAht30Sensor() {
    return std::make_unique<Aht30Sensor>();
}

}  // namespace air360
