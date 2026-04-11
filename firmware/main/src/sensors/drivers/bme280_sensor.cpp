#include "air360/sensors/drivers/bme280_sensor.hpp"

#include <cstdio>
#include <cstdint>
#include <memory>
#include <string>

extern "C" {
#include "bme280.h"
}

#include "air360/sensors/transport_binding.hpp"
#include "esp_timer.h"

namespace air360 {

struct Bme280DriverState {
    i2c_bus_handle_t bus = nullptr;
    bme280_handle_t sensor = nullptr;
};

namespace {

constexpr bme280_sensor_sampling kOversampling = BME280_SAMPLING_X1;
constexpr bme280_sensor_filter kFilter = BME280_FILTER_OFF;
constexpr bme280_standby_duration kStandbyDuration = BME280_STANDBY_MS_0_5;

std::string formatChipId(std::uint8_t chip_id) {
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "0x%02x", static_cast<unsigned>(chip_id));
    return buffer;
}

}  // namespace

Bme280Sensor::~Bme280Sensor() {
    destroyState();
}

SensorType Bme280Sensor::type() const {
    return SensorType::kBme280;
}

esp_err_t Bme280Sensor::init(
    const SensorRecord& record,
    const SensorDriverContext& context) {
    measurement_.clear();
    last_error_.clear();
    initialized_ = false;

    if (context.i2c_bus_manager == nullptr) {
        setError("I2C bus manager is unavailable.");
        return ESP_ERR_INVALID_STATE;
    }

    destroyState();

    const esp_err_t probe_err =
        context.i2c_bus_manager->probe(record.i2c_bus_id, record.i2c_address);
    if (probe_err != ESP_OK) {
        setError("BME280 did not respond on the selected I2C bus and address.");
        return probe_err == ESP_ERR_TIMEOUT ? ESP_ERR_NOT_FOUND : probe_err;
    }

    std::uint8_t chip_id = 0U;
    const esp_err_t chip_id_err =
        context.i2c_bus_manager->readRegister(
            record.i2c_bus_id,
            record.i2c_address,
            BME280_REGISTER_CHIPID,
            &chip_id,
            1U);
    if (chip_id_err != ESP_OK) {
        setError("BME280 did not respond on the selected I2C bus and address.");
        return chip_id_err == ESP_ERR_TIMEOUT ? ESP_ERR_NOT_FOUND : chip_id_err;
    }

    if (chip_id != BME280_DEFAULT_CHIPID) {
        setError(
            std::string("Unexpected chip id for BME280: ") + formatChipId(chip_id) + ".");
        return ESP_ERR_INVALID_RESPONSE;
    }

    state_ = new Bme280DriverState{};
    const esp_err_t bus_err =
        context.i2c_bus_manager->getComponentBus(record.i2c_bus_id, state_->bus);
    if (bus_err != ESP_OK || state_->bus == nullptr) {
        setError("Failed to bind BME280 to the shared I2C bus.");
        destroyState();
        return bus_err != ESP_OK ? bus_err : ESP_FAIL;
    }

    state_->sensor = bme280_create(state_->bus, record.i2c_address);
    if (state_->sensor == nullptr) {
        setError("Failed to allocate the BME280 component driver.");
        destroyState();
        return ESP_ERR_NO_MEM;
    }

    const esp_err_t init_err = bme280_default_init(state_->sensor);
    if (init_err != ESP_OK) {
        setError("Failed to initialize BME280.");
        destroyState();
        return init_err;
    }

    const esp_err_t configure_err = configureSensor();
    if (configure_err != ESP_OK) {
        destroyState();
        return configure_err;
    }

    initialized_ = true;
    return ESP_OK;
}

esp_err_t Bme280Sensor::poll() {
    if (!initialized_ || state_ == nullptr) {
        setError("BME280 is not initialized.");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = bme280_take_forced_measurement(state_->sensor);
    if (err != ESP_OK) {
        setError("Failed to start BME280 forced measurement.");
        initialized_ = false;
        return err;
    }

    float temperature = 0.0F;
    err = bme280_read_temperature(state_->sensor, &temperature);
    if (err != ESP_OK) {
        setError("Failed to read BME280 temperature.");
        initialized_ = false;
        return err;
    }

    float humidity = 0.0F;
    err = bme280_read_humidity(state_->sensor, &humidity);
    if (err != ESP_OK) {
        setError("Failed to read BME280 humidity.");
        initialized_ = false;
        return err;
    }

    float pressure_hpa = 0.0F;
    err = bme280_read_pressure(state_->sensor, &pressure_hpa);
    if (err != ESP_OK) {
        setError("Failed to read BME280 pressure.");
        initialized_ = false;
        return err;
    }

    measurement_.clear();
    measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
    measurement_.addValue(SensorValueKind::kTemperatureC, temperature);
    measurement_.addValue(SensorValueKind::kHumidityPercent, humidity);
    measurement_.addValue(SensorValueKind::kPressureHpa, pressure_hpa);
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
        state_->sensor,
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

void Bme280Sensor::destroyState() {
    if (state_ == nullptr) {
        return;
    }

    // The shared bus belongs to I2cBusManager; this driver only owns the device handle.
    if (state_->sensor != nullptr) {
        bme280_delete(&state_->sensor);
    }

    delete state_;
    state_ = nullptr;
    initialized_ = false;
}

void Bme280Sensor::setError(const std::string& message) {
    last_error_ = message;
}

std::unique_ptr<SensorDriver> createBme280Sensor() {
    return std::make_unique<Bme280Sensor>();
}

}  // namespace air360
