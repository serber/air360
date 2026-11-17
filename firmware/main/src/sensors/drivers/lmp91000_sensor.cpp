#include "air360/sensors/drivers/lmp91000_sensor.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

#include "air360/sensors/transport_binding.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.sensor.lmp91000";
constexpr std::uint32_t kI2cSpeedHz = 100000U;

// ADS1115 address on the LMP91000 AFE board (ADDR tied to VDD). The LMP91000
// address is fixed by silicon at 0x48, so the two never collide.
constexpr std::uint8_t kAds1115Address = 0x49U;

// LMP91000 registers (datasheet SNAS506, section 7.6).
constexpr std::uint8_t kLmpRegStatus = 0x00U;
constexpr std::uint8_t kLmpRegLock = 0x01U;
constexpr std::uint8_t kLmpRegTiacn = 0x10U;
constexpr std::uint8_t kLmpRegRefcn = 0x11U;
constexpr std::uint8_t kLmpRegModecn = 0x12U;
constexpr std::uint8_t kLmpStatusReady = 0x01U;
constexpr std::uint8_t kLmpLockUnlocked = 0x00U;
constexpr std::uint8_t kLmpLockLocked = 0x01U;
// TIA_GAIN = 350 kOhm (0b111 << 2), RLOAD = 10 Ohm (0b00).
constexpr std::uint8_t kLmpTiacn = 0x1CU;
// REF_SOURCE = external (bit 7), INT_Z = 67 % (0b10 << 5), BIAS_SIGN = 0,
// BIAS = 0 %: Winsen ME3 cells run unbiased.
constexpr std::uint8_t kLmpRefcn = 0xC0U;
// FET_SHORT = off, OP_MODE = 3-lead amperometric cell.
constexpr std::uint8_t kLmpModecn = 0x03U;
constexpr int kLmpReadyAttempts = 20;
constexpr TickType_t kLmpReadyRetryTicks = pdMS_TO_TICKS(10U);

// Internal zero for INT_Z = 67 %, as a fraction of the external reference.
constexpr float kLmpInternalZeroFraction = 0.67F;

// ADS1115 registers and single-shot configurations: PGA +/-4.096 V (125 uV/LSB,
// covers the 2.5 V reference), 8 SPS for the lowest noise, comparator disabled.
constexpr std::uint8_t kAdsRegConversion = 0x00U;
constexpr std::uint8_t kAdsRegConfig = 0x01U;
constexpr std::uint16_t kAdsConfigStartAin0 = 0xC303U;  // MUX = AIN0 vs GND (VOUT)
constexpr std::uint16_t kAdsConfigStartAin1 = 0xD303U;  // MUX = AIN1 vs GND (VREF)
constexpr std::uint16_t kAdsConfigIdle = 0x8000U;       // OS bit: no conversion running
constexpr float kAdsVoltsPerLsb = 4.096F / 32768.0F;
constexpr TickType_t kAdsConversionTicks = pdMS_TO_TICKS(135U);
constexpr int kAdsReadyAttempts = 5;
constexpr TickType_t kAdsReadyRetryTicks = pdMS_TO_TICKS(10U);

// Accepted window for the measured external reference (LM4040-2.5).
constexpr float kMinVrefVolts = 2.3F;
constexpr float kMaxVrefVolts = 2.7F;

// The cell needs time to settle after the LMP91000 starts driving it; samples
// taken earlier are dominated by the charging transient.
constexpr std::uint64_t kWarmupMs = 5ULL * 60ULL * 1000ULL;

std::uint64_t nowMs() {
    return static_cast<std::uint64_t>(esp_timer_get_time() / 1000LL);
}

void releaseDevice(i2c_dev_t& dev, bool& initialized, const char* name) {
    if (initialized) {
        if (esp_err_t err = i2c_dev_delete_mutex(&dev); err != ESP_OK) {
            ESP_LOGW(kTag, "Failed to delete %s device mutex: %s", name, esp_err_to_name(err));
        }
    }
    std::memset(&dev, 0, sizeof(dev));
    initialized = false;
}

}  // namespace

Lmp91000Sensor::~Lmp91000Sensor() {
    teardown();
}

SensorType Lmp91000Sensor::type() const {
    return SensorType::kLmp91000;
}

esp_err_t Lmp91000Sensor::init(
    const SensorRecord& record,
    const SensorDriverContext& context) {
    teardown();
    measurement_.clear();
    clearError();
    soft_fail_policy_.onPollOk();

    esp_err_t err = context.i2c_bus_manager->setupDevice(record, kI2cSpeedHz, lmp_dev_);
    if (err != ESP_OK) {
        setError(std::string("Failed to set up I2C device for LMP91000: ") + esp_err_to_name(err));
        teardown();
        return err;
    }
    lmp_dev_initialized_ = true;

    SensorRecord ads_record = record;
    ads_record.i2c_address = kAds1115Address;
    err = context.i2c_bus_manager->setupDevice(ads_record, kI2cSpeedHz, ads_dev_);
    if (err != ESP_OK) {
        setError(std::string("Failed to set up I2C device for ADS1115: ") + esp_err_to_name(err));
        teardown();
        return err;
    }
    ads_dev_initialized_ = true;

    if (i2c_dev_check_present(&lmp_dev_) != ESP_OK) {
        setError("LMP91000 did not respond on the selected I2C bus and address.");
        teardown();
        return ESP_ERR_NOT_FOUND;
    }
    if (i2c_dev_check_present(&ads_dev_) != ESP_OK) {
        setError("ADS1115 did not respond at 0x49 on the selected I2C bus.");
        teardown();
        return ESP_ERR_NOT_FOUND;
    }

    err = waitLmpReady();
    if (err == ESP_OK) {
        err = configureLmp();
    }
    if (err != ESP_OK) {
        teardown();
        return err;
    }

    configured_at_ms_ = nowMs();
    initialized_ = true;
    ESP_LOGI(kTag, "LMP91000 configured: TIA 350k, external ref, zero 67%%, 3-lead mode");
    return ESP_OK;
}

esp_err_t Lmp91000Sensor::poll() {
    if (!initialized_ || !lmp_dev_initialized_ || !ads_dev_initialized_) {
        setError("LMP91000 AFE is not initialized.");
        return ESP_ERR_INVALID_STATE;
    }

    float vref_volts = 0.0F;
    if (esp_err_t err = readAdsChannelVolts(kAdsConfigStartAin1, vref_volts); err != ESP_OK) {
        return reportPollFailure(
            kTag,
            std::string("Failed to read reference voltage: ") + esp_err_to_name(err),
            err);
    }
    if (vref_volts < kMinVrefVolts || vref_volts > kMaxVrefVolts) {
        return reportPollFailure(
            kTag,
            "Reference voltage " + std::to_string(vref_volts) + " V is out of range.",
            ESP_ERR_INVALID_RESPONSE);
    }

    float vout_volts = 0.0F;
    if (esp_err_t err = readAdsChannelVolts(kAdsConfigStartAin0, vout_volts); err != ESP_OK) {
        return reportPollFailure(
            kTag,
            std::string("Failed to read sensor output: ") + esp_err_to_name(err),
            err);
    }

    const std::uint64_t elapsed_ms = nowMs() - configured_at_ms_;
    if (elapsed_ms < kWarmupMs) {
        const std::uint64_t remaining_s = (kWarmupMs - elapsed_ms + 999U) / 1000U;
        soft_fail_policy_.onPollOk();
        setError("Warming up (" + std::to_string(remaining_s) + " s left).");
        return ESP_OK;
    }

    // Signed cell signal relative to the LMP91000 internal zero. Divide by the
    // 350 kOhm TIA gain for the cell current; the sign depends on the gas.
    const float zero_volts = vref_volts * kLmpInternalZeroFraction;
    const float signal_mv = (vout_volts - zero_volts) * 1000.0F;

    measurement_.clear();
    measurement_.sample_time_ms = nowMs();
    measurement_.addValue(SensorValueKind::kVoltageMv, signal_mv);
    notePollSuccess();
    return ESP_OK;
}

SensorMeasurement Lmp91000Sensor::latestMeasurement() const {
    return measurement_;
}

esp_err_t Lmp91000Sensor::waitLmpReady() {
    std::uint8_t status = 0U;
    for (int attempt = 0; attempt < kLmpReadyAttempts; ++attempt) {
        const esp_err_t err = readLmpRegister(kLmpRegStatus, status);
        if (err != ESP_OK) {
            setError(std::string("Failed to read LMP91000 status: ") + esp_err_to_name(err));
            return err;
        }
        if ((status & kLmpStatusReady) != 0U) {
            return ESP_OK;
        }
        vTaskDelay(kLmpReadyRetryTicks);
    }
    setError("LMP91000 did not report ready.");
    return ESP_ERR_TIMEOUT;
}

esp_err_t Lmp91000Sensor::configureLmp() {
    struct RegisterWrite {
        std::uint8_t reg;
        std::uint8_t value;
    };
    // TIACN and REFCN are write-protected until LOCK is cleared; MODECN is not.
    constexpr std::array<RegisterWrite, 5U> kSequence{{
        {kLmpRegLock, kLmpLockUnlocked},
        {kLmpRegTiacn, kLmpTiacn},
        {kLmpRegRefcn, kLmpRefcn},
        {kLmpRegLock, kLmpLockLocked},
        {kLmpRegModecn, kLmpModecn},
    }};
    for (const RegisterWrite& write : kSequence) {
        if (esp_err_t err = writeLmpRegister(write.reg, write.value); err != ESP_OK) {
            setError(std::string("Failed to configure LMP91000: ") + esp_err_to_name(err));
            return err;
        }
    }

    constexpr std::array<RegisterWrite, 3U> kExpected{{
        {kLmpRegTiacn, kLmpTiacn},
        {kLmpRegRefcn, kLmpRefcn},
        {kLmpRegModecn, kLmpModecn},
    }};
    for (const RegisterWrite& expected : kExpected) {
        std::uint8_t value = 0U;
        if (esp_err_t err = readLmpRegister(expected.reg, value); err != ESP_OK) {
            setError(std::string("Failed to verify LMP91000 configuration: ") + esp_err_to_name(err));
            return err;
        }
        if (value != expected.value) {
            setError("LMP91000 configuration did not read back as written.");
            return ESP_ERR_INVALID_RESPONSE;
        }
    }
    return ESP_OK;
}

esp_err_t Lmp91000Sensor::writeLmpRegister(std::uint8_t reg, std::uint8_t value) {
    return i2c_dev_write_reg(&lmp_dev_, reg, &value, 1U);
}

esp_err_t Lmp91000Sensor::readLmpRegister(std::uint8_t reg, std::uint8_t& out_value) {
    return i2c_dev_read_reg(&lmp_dev_, reg, &out_value, 1U);
}

esp_err_t Lmp91000Sensor::readAdsChannelVolts(std::uint16_t config, float& out_volts) {
    const std::array<std::uint8_t, 2U> config_bytes{
        static_cast<std::uint8_t>((config >> 8U) & 0xFFU),
        static_cast<std::uint8_t>(config & 0xFFU),
    };
    esp_err_t err = i2c_dev_write_reg(
        &ads_dev_, kAdsRegConfig, config_bytes.data(), config_bytes.size());
    if (err != ESP_OK) {
        return err;
    }

    vTaskDelay(kAdsConversionTicks);

    std::array<std::uint8_t, 2U> data{};
    bool ready = false;
    for (int attempt = 0; attempt < kAdsReadyAttempts && !ready; ++attempt) {
        err = i2c_dev_read_reg(&ads_dev_, kAdsRegConfig, data.data(), data.size());
        if (err != ESP_OK) {
            return err;
        }
        ready = (data[0] & static_cast<std::uint8_t>(kAdsConfigIdle >> 8U)) != 0U;
        if (!ready) {
            vTaskDelay(kAdsReadyRetryTicks);
        }
    }
    if (!ready) {
        return ESP_ERR_TIMEOUT;
    }

    err = i2c_dev_read_reg(&ads_dev_, kAdsRegConversion, data.data(), data.size());
    if (err != ESP_OK) {
        return err;
    }
    const auto raw = static_cast<std::int16_t>(
        (static_cast<std::uint16_t>(data[0]) << 8U) | data[1]);
    out_volts = static_cast<float>(raw) * kAdsVoltsPerLsb;
    return ESP_OK;
}

void Lmp91000Sensor::teardown() {
    initialized_ = false;
    soft_fail_policy_.onPollOk();
    configured_at_ms_ = 0U;
    releaseDevice(lmp_dev_, lmp_dev_initialized_, "LMP91000");
    releaseDevice(ads_dev_, ads_dev_initialized_, "ADS1115");
}

std::unique_ptr<SensorDriver> createLmp91000Sensor() {
    return std::make_unique<Lmp91000Sensor>();
}

}  // namespace air360
