#include "air360/sensors/drivers/mhz19b_sensor.hpp"

#include "air360/sensors/transport_binding.hpp"

#include <cstdint>
#include <memory>
#include <string>

#include "esp_log.h"
#include "esp_timer.h"
#include "mhz19b.h"

namespace air360 {

namespace {
constexpr char kTag[] = "air360.sensor.mhz19b";
// MH-Z19B UART protocol is fixed at 9600 8N1.
constexpr std::uint32_t kMhz19bBaudRate = 9600U;
}  // namespace

Mhz19bSensor::~Mhz19bSensor() {
    teardown();
}

SensorType Mhz19bSensor::type() const {
    return SensorType::kMhz19b;
}

esp_err_t Mhz19bSensor::init(const SensorRecord& record, const SensorDriverContext& context) {
    teardown();
    measurement_.clear();
    clearError();
    soft_fail_policy_.onPollOk();

    uart_port_manager_ = context.uart_port_manager;
    if (uart_port_manager_ == nullptr) {
        setError("UART port manager is unavailable.");
        return ESP_ERR_INVALID_STATE;
    }

    // UartPortManager is the single owner of the UART driver: it is installed
    // here (or verified if already open with the same settings) and deleted by
    // SensorManager::stop(). mhz19b_init() is not used because it installs the
    // UART itself and mhz19b_free() never uninstalls it.
    const esp_err_t open_err = uart_port_manager_->open(
        record.uart_port_id,
        record.uart_rx_gpio_pin,
        record.uart_tx_gpio_pin,
        kMhz19bBaudRate);
    if (open_err != ESP_OK) {
        setError(std::string("Failed to open configured UART for MH-Z19B: ") +
                 esp_err_to_name(open_err));
        return open_err;
    }

    // Same device state mhz19b_init() would set up, with a driver-owned buffer.
    device_.uart_port = static_cast<uart_port_t>(record.uart_port_id);
    device_.buf = rx_buffer_.data();
    device_.last_value = -1;
    device_.last_ts = esp_timer_get_time();

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
    // Nothing to release here: the buffer is a member and the UART belongs to
    // UartPortManager. Never call mhz19b_free() on the member buffer.
    initialized_ = false;
    soft_fail_policy_.onPollOk();
    device_ = mhz19b_dev_t{};
}

std::unique_ptr<SensorDriver> createMhz19bSensor() {
    return std::make_unique<Mhz19bSensor>();
}

}  // namespace air360
