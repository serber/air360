#include "air360/sensors/drivers/bmp390_sensor.hpp"

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>

#include "air360/sensors/transport_binding.hpp"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.sensor.bmp390";

}  // namespace

Bmp390Sensor::~Bmp390Sensor() {
    teardown();
}

SensorType Bmp390Sensor::type() const {
    return SensorType::kBmp390;
}

esp_err_t Bmp390Sensor::init(const SensorRecord& record, const SensorDriverContext& context) {
    teardown();
    measurement_.clear();
    clearError();
    soft_fail_policy_.onPollOk();

    i2c_master_bus_handle_t bus_handle = nullptr;
    esp_err_t err = context.i2c_bus_manager->getMasterBusHandle(record.i2c_bus_id, bus_handle);
    if (err != ESP_OK) {
        setError(std::string("Failed to get I2C master bus handle for BMP390: ") + esp_err_to_name(err));
        return err;
    }

    // Mirrors I2C_BMP390_CONFIG_DEFAULT but set field-by-field: the component's
    // default macro uses out-of-declaration-order designators that trip the
    // project's -Werror=missing-field-initializers. Forced power mode makes
    // bmp390_get_measurements() re-trigger a one-shot conversion on each poll,
    // which suits the multi-second polling model.
    bmp390_config_t config{};
    config.i2c_address = record.i2c_address;
    config.i2c_clock_speed = I2C_BMP390_DEV_CLK_SPD;
    config.iir_filter = BMP390_IIR_FILTER_OFF;
    config.pressure_oversampling = BMP390_PRESSURE_OVERSAMPLING_8X;
    config.temperature_oversampling = BMP390_TEMPERATURE_OVERSAMPLING_8X;
    config.output_data_rate = BMP390_ODR_40MS;
    config.power_mode = BMP390_POWER_MODE_FORCED;

    err = bmp390_init(bus_handle, &config, &handle_);
    if (err != ESP_OK) {
        setError(std::string("Failed to initialize BMP390 (chip ID mismatch or I2C error): ") +
                 esp_err_to_name(err));
        handle_ = nullptr;
        return err;
    }

    initialized_ = true;
    return ESP_OK;
}

esp_err_t Bmp390Sensor::poll() {
    if (!initialized_ || handle_ == nullptr) {
        setError("BMP390 sensor is not initialized.");
        return ESP_ERR_INVALID_STATE;
    }

    float temperature_c = 0.0F;
    float pressure_pa = 0.0F;
    if (esp_err_t err = bmp390_get_measurements(handle_, &temperature_c, &pressure_pa);
        err != ESP_OK) {
        return reportPollFailure(
            kTag,
            std::string("Failed to read BMP390 measurement: ") + esp_err_to_name(err),
            err);
    }

    if (std::isnan(temperature_c) || std::isnan(pressure_pa)) {
        return reportPollFailure(kTag, "BMP390 returned invalid values.", ESP_ERR_INVALID_RESPONSE);
    }

    measurement_.clear();
    measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
    measurement_.addValue(SensorValueKind::kTemperatureC, temperature_c);
    measurement_.addValue(SensorValueKind::kPressureHpa, pressure_pa / kPaPerHpa);
    notePollSuccess();
    return ESP_OK;
}

SensorMeasurement Bmp390Sensor::latestMeasurement() const {
    return measurement_;
}

void Bmp390Sensor::teardown() {
    initialized_ = false;
    soft_fail_policy_.onPollOk();
    if (handle_ != nullptr) {
        // bmp390_delete() releases the device from the shared I2C master bus;
        // the bus itself is owned by I2cBusManager and must not be deleted here.
        if (esp_err_t err = bmp390_delete(handle_); err != ESP_OK) {
            ESP_LOGW(kTag, "Failed to delete BMP390 device: %s", esp_err_to_name(err));
        }
        handle_ = nullptr;
    }
}

std::unique_ptr<SensorDriver> createBmp390Sensor() {
    return std::make_unique<Bmp390Sensor>();
}

}  // namespace air360
