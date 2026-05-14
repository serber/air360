#include "air360/sensors/drivers/ppd42ns_sensor.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.sensor.ppd42ns";
constexpr std::uint64_t kMinimumSampleWindowUs = 30ULL * 1000ULL * 1000ULL;
constexpr std::uint64_t kWarmupUs = 60ULL * 1000ULL * 1000ULL;

float concentrationPcs001Cf(float ratio_percent) {
    const float concentration =
        (1.1F * ratio_percent * ratio_percent * ratio_percent) -
        (3.8F * ratio_percent * ratio_percent) +
        (520.0F * ratio_percent) + 0.62F;
    return std::max(0.0F, concentration);
}

}  // namespace

Ppd42nsSensor::Ppd42nsSensor() = default;

Ppd42nsSensor::~Ppd42nsSensor() {
    const esp_err_t err = detachInterrupt();
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Failed to detach PPD42NS GPIO interrupt: %s", esp_err_to_name(err));
    }
}

SensorType Ppd42nsSensor::type() const {
    return SensorType::kPpd42ns;
}

esp_err_t Ppd42nsSensor::init(
    const SensorRecord& record,
    const SensorDriverContext& context) {
    // PPD42NS uses its configured GPIO directly and does not need the shared driver context.
    static_cast<void>(context);

    const esp_err_t detach_err = detachInterrupt();
    if (detach_err != ESP_OK) {
        return detach_err;
    }

    record_ = record;
    measurement_.clear();
    clearError();
    soft_fail_policy_.onPollOk();
    initialized_ = false;

    if (record_.analog_gpio_pin < 0) {
        setError("PPD42NS GPIO pin is not configured.");
        return ESP_ERR_INVALID_ARG;
    }

    gpio_pin_ = static_cast<gpio_num_t>(record_.analog_gpio_pin);

    gpio_config_t config{};
    config.pin_bit_mask = 1ULL << static_cast<std::uint32_t>(gpio_pin_);
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_ANYEDGE;

    if (esp_err_t err = gpio_config(&config); err != ESP_OK) {
        setError(std::string("PPD42NS GPIO config failed: ") + esp_err_to_name(err) + ".");
        return err;
    }

    const esp_err_t install_err = gpio_install_isr_service(0);
    if (install_err != ESP_OK && install_err != ESP_ERR_INVALID_STATE) {
        setError(
            std::string("PPD42NS GPIO ISR service install failed: ") +
            esp_err_to_name(install_err) + ".");
        return install_err;
    }

    if (esp_err_t err = gpio_isr_handler_add(gpio_pin_, &Ppd42nsSensor::isrHandler, this);
        err != ESP_OK) {
        setError(std::string("PPD42NS GPIO ISR attach failed: ") + esp_err_to_name(err) + ".");
        return err;
    }

    isr_attached_ = true;
    const std::uint64_t now_us = static_cast<std::uint64_t>(esp_timer_get_time());
    initialized_at_us_ = now_us;
    resetAccumulation(now_us);
    initialized_ = true;
    return ESP_OK;
}

esp_err_t Ppd42nsSensor::poll() {
    if (!initialized_) {
        setError("PPD42NS sensor is not initialized.");
        return ESP_ERR_INVALID_STATE;
    }

    const std::uint64_t now_us = static_cast<std::uint64_t>(esp_timer_get_time());
    if ((now_us - initialized_at_us_) < kWarmupUs) {
        resetAccumulation(now_us);
        setError("PPD42NS is warming up.");
        soft_fail_policy_.onPollOk();
        return ESP_OK;
    }

    std::uint64_t low_us = 0U;
    std::uint64_t window_us = 0U;

    portENTER_CRITICAL(&mux_);
    if (low_start_us_ != 0U) {
        accumulated_low_us_ += now_us - low_start_us_;
        low_start_us_ = now_us;
    }
    low_us = accumulated_low_us_;
    window_us = now_us - sample_start_us_;
    accumulated_low_us_ = 0U;
    sample_start_us_ = now_us;
    portEXIT_CRITICAL(&mux_);

    if (window_us < kMinimumSampleWindowUs) {
        setError("PPD42NS sample window is still accumulating.");
        soft_fail_policy_.onPollOk();
        return ESP_OK;
    }

    if (low_us > window_us) {
        return reportPollFailure(kTag, "PPD42NS low pulse time exceeded sample window.", ESP_FAIL);
    }

    const float ratio_percent =
        (static_cast<float>(low_us) / static_cast<float>(window_us)) * 100.0F;
    const float concentration = concentrationPcs001Cf(ratio_percent);

    if (!std::isfinite(ratio_percent) || !std::isfinite(concentration)) {
        return reportPollFailure(kTag, "PPD42NS calculation returned invalid values.", ESP_FAIL);
    }

    measurement_.clear();
    measurement_.sample_time_ms = now_us / 1000ULL;
    measurement_.addValue(SensorValueKind::kDustConcentrationPcs001Cf, concentration);
    measurement_.addValue(SensorValueKind::kLowPulseOccupancyPercent, ratio_percent);
    notePollSuccess();
    return ESP_OK;
}

SensorMeasurement Ppd42nsSensor::latestMeasurement() const {
    return measurement_;
}

void IRAM_ATTR Ppd42nsSensor::isrHandler(void* arg) {
    auto* sensor = static_cast<Ppd42nsSensor*>(arg);
    const std::uint64_t now_us = static_cast<std::uint64_t>(esp_timer_get_time());
    const int level = gpio_get_level(sensor->gpio_pin_);
    sensor->handleEdgeFromIsr(now_us, level);
}

void Ppd42nsSensor::handleEdgeFromIsr(std::uint64_t now_us, int level) {
    portENTER_CRITICAL_ISR(&mux_);
    if (level == 0) {
        if (low_start_us_ == 0U) {
            low_start_us_ = now_us;
        }
    } else if (low_start_us_ != 0U) {
        accumulated_low_us_ += now_us - low_start_us_;
        low_start_us_ = 0U;
    }
    portEXIT_CRITICAL_ISR(&mux_);
}

void Ppd42nsSensor::resetAccumulation(std::uint64_t now_us) {
    const int level = gpio_pin_ != GPIO_NUM_NC ? gpio_get_level(gpio_pin_) : 1;
    portENTER_CRITICAL(&mux_);
    accumulated_low_us_ = 0U;
    low_start_us_ = level == 0 ? now_us : 0U;
    sample_start_us_ = now_us;
    portEXIT_CRITICAL(&mux_);
}

esp_err_t Ppd42nsSensor::detachInterrupt() {
    if (!isr_attached_) {
        return ESP_OK;
    }

    const esp_err_t disable_err = gpio_intr_disable(gpio_pin_);
    const esp_err_t remove_err = gpio_isr_handler_remove(gpio_pin_);
    isr_attached_ = false;
    gpio_pin_ = GPIO_NUM_NC;

    if (disable_err != ESP_OK) {
        setError(
            std::string("PPD42NS GPIO interrupt disable failed: ") +
            esp_err_to_name(disable_err) + ".");
        return disable_err;
    }
    if (remove_err != ESP_OK) {
        setError(
            std::string("PPD42NS GPIO ISR remove failed: ") + esp_err_to_name(remove_err) + ".");
        return remove_err;
    }
    return ESP_OK;
}

std::unique_ptr<SensorDriver> createPpd42nsSensor() {
    return std::make_unique<Ppd42nsSensor>();
}

}  // namespace air360
