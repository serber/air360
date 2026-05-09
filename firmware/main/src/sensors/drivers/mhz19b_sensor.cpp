#include "air360/sensors/drivers/mhz19b_sensor.hpp"

#include <cstring>
#include <memory>
#include <string>

#include "esp_log.h"
#include "esp_timer.h"
#include "mhz19b.h"

namespace air360 {

namespace {
constexpr char kTag[] = "air360.sensor.mhz19b";
}  // namespace

Mhz19bSensor::~Mhz19bSensor() {
    teardown();
}

SensorType Mhz19bSensor::type() const {
    return SensorType::kMhz19b;
}

esp_err_t Mhz19bSensor::init(const SensorRecord& record, const SensorDriverContext& context) {
    // MH-Z19B owns its UART setup through the component driver; context is not needed.
    static_cast<void>(context);
    teardown();
    measurement_.clear();
    clearError();
    soft_fail_policy_.onPollOk();

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
    return ESP_OK;
}

esp_err_t Mhz19bSensor::poll() {
    if (!initialized_) {
        setError("MH-Z19B is not initialized.");
        return ESP_ERR_INVALID_STATE;
    }

    if (mhz19b_is_warming_up(&device_, false)) {
        soft_fail_policy_.onPollOk();
        setError("Warming up (3 min).");
        return ESP_OK;
    }

    if (!mhz19b_is_ready(&device_)) {
        soft_fail_policy_.onPollOk();
        return ESP_OK;
    }

    std::int16_t co2 = 0;
    if (esp_err_t err = mhz19b_read_co2(&device_, &co2); err != ESP_OK) {
        return reportPollFailure(
            kTag,
            std::string("Failed to read MH-Z19B CO2: ") + esp_err_to_name(err),
            err);
    }

    if (co2 <= 0 || co2 >= 5000) {
        soft_fail_policy_.onPollOk();
        setError("Warming up.");
        return ESP_OK;
    }

    measurement_.clear();
    measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
    measurement_.addValue(SensorValueKind::kCo2Ppm, static_cast<float>(co2));
    notePollSuccess();
    return ESP_OK;
}

SensorMeasurement Mhz19bSensor::latestMeasurement() const {
    return measurement_;
}

void Mhz19bSensor::teardown() {
    if (initialized_) {
        mhz19b_free(&device_);
    }
    initialized_ = false;
    soft_fail_policy_.onPollOk();
    std::memset(&device_, 0, sizeof(device_));
}

std::unique_ptr<SensorDriver> createMhz19bSensor() {
    return std::make_unique<Mhz19bSensor>();
}

}  // namespace air360
