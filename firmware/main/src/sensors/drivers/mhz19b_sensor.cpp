#include "air360/sensors/drivers/mhz19b_sensor.hpp"

#include <cstring>
#include <memory>
#include <string>

#include "esp_timer.h"
#include "mhz19b.h"

namespace air360 {

Mhz19bSensor::~Mhz19bSensor() {
    reset();
}

SensorType Mhz19bSensor::type() const {
    return SensorType::kMhz19b;
}

esp_err_t Mhz19bSensor::init(const SensorRecord& record, const SensorDriverContext& context) {
    // MH-Z19B owns its UART setup through the component driver; context is not needed.
    static_cast<void>(context);
    reset();
    measurement_.clear();
    last_error_.clear();
    poll_failure_count_ = 0U;

    std::memset(&device_, 0, sizeof(device_));

    const esp_err_t err = mhz19b_init(
        &device_,
        static_cast<uart_port_t>(record.uart_port_id),
        static_cast<gpio_num_t>(record.uart_tx_gpio_pin),
        static_cast<gpio_num_t>(record.uart_rx_gpio_pin));
    if (err != ESP_OK) {
        setError(std::string("Failed to initialize MH-Z19B: ") + esp_err_to_name(err));
        return err;
    }

    initialized_ = true;
    last_error_.clear();
    return ESP_OK;
}

esp_err_t Mhz19bSensor::poll() {
    if (!initialized_) {
        setError("MH-Z19B is not initialized.");
        return ESP_ERR_INVALID_STATE;
    }

    if (mhz19b_is_warming_up(&device_, false)) {
        poll_failure_count_ = 0U;
        setError("Warming up (3 min).");
        return ESP_OK;
    }

    if (!mhz19b_is_ready(&device_)) {
        poll_failure_count_ = 0U;
        return ESP_OK;
    }

    std::int16_t co2 = 0;
    const esp_err_t err = mhz19b_read_co2(&device_, &co2);
    if (err != ESP_OK) {
        setError(std::string("Failed to read MH-Z19B CO2: ") + esp_err_to_name(err));
        if (++poll_failure_count_ >= kSensorPollFailureReinitThreshold) {
            initialized_ = false;
        }
        return err;
    }

    if (co2 <= 0 || co2 >= 5000) {
        poll_failure_count_ = 0U;
        setError("Warming up.");
        return ESP_OK;
    }

    measurement_.clear();
    measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
    measurement_.addValue(SensorValueKind::kCo2Ppm, static_cast<float>(co2));
    poll_failure_count_ = 0U;
    last_error_.clear();
    return ESP_OK;
}

SensorMeasurement Mhz19bSensor::latestMeasurement() const {
    return measurement_;
}

std::string Mhz19bSensor::lastError() const {
    return last_error_;
}

void Mhz19bSensor::reset() {
    if (initialized_) {
        mhz19b_free(&device_);
    }
    initialized_ = false;
    poll_failure_count_ = 0U;
    std::memset(&device_, 0, sizeof(device_));
}

void Mhz19bSensor::setError(const std::string& message) {
    last_error_ = message;
}

std::unique_ptr<SensorDriver> createMhz19bSensor() {
    return std::make_unique<Mhz19bSensor>();
}

}  // namespace air360
