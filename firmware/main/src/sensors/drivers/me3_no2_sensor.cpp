#include "air360/sensors/drivers/me3_no2_sensor.hpp"

#include <cstdint>
#include <string>

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "esp_timer.h"

namespace air360 {

namespace {

constexpr adc_atten_t kAnalogAttenuation = ADC_ATTEN_DB_12;

bool isSupportedAnalogPin(std::int16_t pin) {
    return pin >= 0;
}

}  // namespace

Me3No2Sensor::~Me3No2Sensor() {
    releaseHandles();
}

SensorType Me3No2Sensor::type() const {
    return SensorType::kMe3No2;
}

esp_err_t Me3No2Sensor::init(
    const SensorRecord& record,
    const SensorDriverContext& context) {
    // ME3-NO2 is an ADC-only driver and does not need the shared driver context.
    static_cast<void>(context);

    releaseHandles();
    record_ = record;
    measurement_.clear();
    last_error_.clear();
    channel_ = -1;
    calibration_enabled_ = false;
    initialized_ = false;

    if (!isSupportedAnalogPin(record.analog_gpio_pin)) {
        setError("Analog GPIO pin is not configured.");
        return ESP_ERR_INVALID_ARG;
    }

    adc_unit_t unit = ADC_UNIT_1;
    adc_channel_t channel = ADC_CHANNEL_0;
    esp_err_t err = adc_oneshot_io_to_channel(
        static_cast<int>(record.analog_gpio_pin),
        &unit,
        &channel);
    if (err != ESP_OK) {
        setError(std::string("ADC channel mapping failed: ") + esp_err_to_name(err) + ".");
        return err;
    }

    adc_oneshot_unit_init_cfg_t unit_cfg{};
    unit_cfg.unit_id = unit;
    err = adc_oneshot_new_unit(&unit_cfg, &adc_handle_);
    if (err != ESP_OK) {
        setError(std::string("ADC unit init failed: ") + esp_err_to_name(err) + ".");
        return err;
    }

    adc_oneshot_chan_cfg_t channel_cfg{};
    channel_cfg.atten = kAnalogAttenuation;
    channel_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    err = adc_oneshot_config_channel(adc_handle_, channel, &channel_cfg);
    if (err != ESP_OK) {
        setError(std::string("ADC channel config failed: ") + esp_err_to_name(err) + ".");
        releaseHandles();
        return err;
    }

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg{};
    cali_cfg.unit_id = unit;
    cali_cfg.atten = kAnalogAttenuation;
    cali_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali_handle_) == ESP_OK) {
        calibration_enabled_ = true;
    }
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_cfg{};
    cali_cfg.unit_id = unit;
    cali_cfg.atten = kAnalogAttenuation;
    cali_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    if (adc_cali_create_scheme_line_fitting(&cali_cfg, &cali_handle_) == ESP_OK) {
        calibration_enabled_ = true;
    }
#endif

    channel_ = static_cast<int>(channel);
    initialized_ = true;
    return ESP_OK;
}

esp_err_t Me3No2Sensor::poll() {
    if (!initialized_ || adc_handle_ == nullptr || channel_ < 0) {
        setError("ME3-NO2 sensor is not initialized.");
        return ESP_ERR_INVALID_STATE;
    }

    int raw = 0;
    const esp_err_t err = adc_oneshot_read(
        adc_handle_,
        static_cast<adc_channel_t>(channel_),
        &raw);
    if (err != ESP_OK) {
        setError(std::string("ADC read failed: ") + esp_err_to_name(err) + ".");
        return err;
    }

    measurement_.clear();
    measurement_.sample_time_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000ULL);
    measurement_.addValue(SensorValueKind::kAdcRaw, static_cast<float>(raw));

    if (calibration_enabled_ && cali_handle_ != nullptr) {
        int voltage_mv = 0;
        if (adc_cali_raw_to_voltage(cali_handle_, raw, &voltage_mv) == ESP_OK) {
            measurement_.addValue(SensorValueKind::kVoltageMv, static_cast<float>(voltage_mv));
        }
    }

    last_error_.clear();
    return ESP_OK;
}

SensorMeasurement Me3No2Sensor::latestMeasurement() const {
    return measurement_;
}

std::string Me3No2Sensor::lastError() const {
    return last_error_;
}

void Me3No2Sensor::setError(const std::string& message) {
    last_error_ = message;
}

void Me3No2Sensor::releaseHandles() {
    if (cali_handle_ != nullptr) {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_delete_scheme_curve_fitting(cali_handle_);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
        adc_cali_delete_scheme_line_fitting(cali_handle_);
#endif
        cali_handle_ = nullptr;
    }

    if (adc_handle_ != nullptr) {
        adc_oneshot_del_unit(adc_handle_);
        adc_handle_ = nullptr;
    }

    calibration_enabled_ = false;
    initialized_ = false;
    channel_ = -1;
}

std::unique_ptr<SensorDriver> createMe3No2Sensor() {
    return std::make_unique<Me3No2Sensor>();
}

}  // namespace air360
