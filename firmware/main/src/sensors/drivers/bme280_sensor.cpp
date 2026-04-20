#include "air360/sensors/drivers/bme280_sensor.hpp"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

extern "C" {
#include "bme280.h"
}

#include "air360/sensors/transport_binding.hpp"
#include "esp_timer.h"

namespace air360 {

namespace {

constexpr bme280_sensor_sampling kOversampling = BME280_SAMPLING_X1;
constexpr bme280_sensor_filter kFilter = BME280_FILTER_OFF;
constexpr bme280_standby_duration kStandbyDuration = BME280_STANDBY_MS_0_5;
constexpr std::uint32_t kBme280I2cSpeedHz = 100000U;

}  // namespace

struct Bme280DriverState {
    i2c_dev_t dev{};
    bool dev_initialized = false;
    i2c_bus_handle_t bus = nullptr;
    bme280_handle_t sensor = nullptr;
};

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

    destroyState();

    state_ = std::make_unique<Bme280DriverState>();

    std::memset(&state_->dev, 0, sizeof(state_->dev));
    esp_err_t err = context.i2c_bus_manager->setupDevice(record, kBme280I2cSpeedHz, state_->dev);
    if (err != ESP_OK) {
        setError(std::string("Failed to set up I2C device for BME280: ") + esp_err_to_name(err));
        destroyState();
        return err;
    }
    state_->dev_initialized = true;

    err = i2c_dev_check_present(&state_->dev);
    if (err != ESP_OK) {
        setError("BME280 did not respond on the selected I2C bus and address.");
        destroyState();
        return ESP_ERR_NOT_FOUND;
    }

    err = context.i2c_bus_manager->getComponentBus(record.i2c_bus_id, state_->bus);
    if (err != ESP_OK) {
        setError("Failed to bind BME280 to the shared I2C bus.");
        destroyState();
        return err;
    }

    state_->sensor = bme280_create(state_->bus, record.i2c_address);
    if (state_->sensor == nullptr) {
        setError("Failed to allocate the BME280 component driver.");
        destroyState();
        return ESP_ERR_NO_MEM;
    }

    // bme280_default_init() reads and validates the chip ID internally.
    err = bme280_default_init(state_->sensor);
    if (err != ESP_OK) {
        setError("Failed to initialize BME280 (chip ID mismatch or I2C error).");
        destroyState();
        return err;
    }

    err = configureSensor();
    if (err != ESP_OK) {
        destroyState();
        return err;
    }

    initialized_ = true;
    return ESP_OK;
}

esp_err_t Bme280Sensor::poll() {
    if (!initialized_ || !state_) {
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
    if (!state_) {
        return;
    }

    if (state_->sensor != nullptr) {
        bme280_delete(&state_->sensor);
    }

    // i2c_bus_create() borrowed the bus from i2cdev — do not delete the bus itself.

    if (state_->dev_initialized) {
        i2c_dev_delete_mutex(&state_->dev);
    }

    state_.reset();
    initialized_ = false;
}

void Bme280Sensor::setError(const std::string& message) {
    last_error_ = message;
}

std::unique_ptr<SensorDriver> createBme280Sensor() {
    return std::make_unique<Bme280Sensor>();
}

}  // namespace air360
