#include "air360/power_gate.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <cstdio>

#include "air360/time_utils.hpp"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace air360 {

namespace {

constexpr char kTag[] = "air360.power_gate";
// The sensor task polls a fresh sensor every 5 s during warmup, so 250 ms is
// fine-grained enough to notice the first sample without spinning.
constexpr std::uint32_t kSamplePollIntervalMs = 250U;
// Doubling stops here because the sleep cap (7200 s) is reached long before.
constexpr std::uint32_t kSleepEscalationShiftCap = 8U;

// RTC slow memory survives deep sleep but not a cold boot, so both values are
// trusted only when the current boot is a deep-sleep wake-up.
RTC_DATA_ATTR std::uint32_t g_rtc_low_voltage_sleeps = 0U;
RTC_DATA_ATTR std::uint32_t g_rtc_last_sleep_seconds = 0U;

std::uint32_t escalatedSleepSeconds(
    const DeviceConfig& config,
    std::uint32_t prior_sleeps) {
    const std::uint32_t shift = std::min(prior_sleeps, kSleepEscalationShiftCap);
    const std::uint64_t seconds =
        static_cast<std::uint64_t>(config.power_gate_sleep_base_s) << shift;
    return static_cast<std::uint32_t>(
        std::min<std::uint64_t>(seconds, config.power_gate_sleep_max_s));
}

bool findPowerMonitorVoltage(
    const SensorConfigList& sensor_config_list,
    const MeasurementStore& measurement_store,
    std::uint32_t& out_sensor_id,
    float& out_voltage_mv) {
    for (std::size_t index = 0; index < sensor_config_list.sensor_count; ++index) {
        const SensorRecord& record = sensor_config_list.sensors[index];
        if (record.enabled == 0U || !sensorTypeIsPowerMonitor(record.sensor_type)) {
            continue;
        }
        const MeasurementRuntimeInfo info = measurement_store.runtimeInfoForSensor(record.id);
        const SensorValue* voltage = info.measurement.findValue(SensorValueKind::kVoltageMv);
        if (voltage != nullptr) {
            out_sensor_id = record.id;
            out_voltage_mv = voltage->value;
            return true;
        }
    }
    return false;
}

bool hasEnabledPowerMonitor(const SensorConfigList& sensor_config_list) {
    for (std::size_t index = 0; index < sensor_config_list.sensor_count; ++index) {
        const SensorRecord& record = sensor_config_list.sensors[index];
        if (record.enabled != 0U && sensorTypeIsPowerMonitor(record.sensor_type)) {
            return true;
        }
    }
    return false;
}

void requestSleep(
    const DeviceConfig& config,
    PowerGateDecision& decision,
    PowerGateOutcome outcome) {
    decision.outcome = outcome;
    decision.sleep_requested = true;
    decision.sleep_seconds = escalatedSleepSeconds(config, decision.prior_sleeps);
    // Persist the escalation state before the caller powers down.
    g_rtc_low_voltage_sleeps = decision.prior_sleeps + 1U;
    g_rtc_last_sleep_seconds = decision.sleep_seconds;
}

}  // namespace

const char* powerGateOutcomeKey(PowerGateOutcome outcome) {
    switch (outcome) {
        case PowerGateOutcome::kDisabled:
            return "disabled";
        case PowerGateOutcome::kNoPowerMonitor:
            return "no_power_monitor";
        case PowerGateOutcome::kNoSample:
            return "no_sample";
        case PowerGateOutcome::kPassed:
            return "passed";
        case PowerGateOutcome::kSleepLowVoltage:
            return "sleep_low_voltage";
        case PowerGateOutcome::kSleepBrownout:
            return "sleep_brownout";
        default:
            return "unknown";
    }
}

PowerGateDecision PowerGate::evaluate(
    const DeviceConfig& config,
    const SensorConfigList& sensor_config_list,
    const MeasurementStore& measurement_store,
    esp_reset_reason_t reset_reason) {
    PowerGateDecision decision;
    decision.enabled = config.power_gate_enabled != 0U;
    decision.threshold_mv = config.power_gate_threshold_mv;

    const bool woke_from_deep_sleep = reset_reason == ESP_RST_DEEPSLEEP;
    if (woke_from_deep_sleep) {
        decision.prior_sleeps = g_rtc_low_voltage_sleeps;
        decision.last_sleep_seconds = g_rtc_last_sleep_seconds;
    } else {
        // Any other reset breaks the escalation chain.
        g_rtc_low_voltage_sleeps = 0U;
        g_rtc_last_sleep_seconds = 0U;
    }

    if (!decision.enabled) {
        decision.outcome = PowerGateOutcome::kDisabled;
        decision.detail = "Power gate disabled";
        g_rtc_low_voltage_sleeps = 0U;
        return decision;
    }

    if (!hasEnabledPowerMonitor(sensor_config_list)) {
        decision.outcome = PowerGateOutcome::kNoPowerMonitor;
        decision.detail = "Power gate enabled but no INA219/INA226 is configured; boot continues";
        ESP_LOGW(kTag, "%s", decision.detail.c_str());
        g_rtc_low_voltage_sleeps = 0U;
        return decision;
    }

    const std::uint64_t started_ms = uptimeMilliseconds();
    const std::uint64_t deadline_ms =
        started_ms + static_cast<std::uint64_t>(config.power_gate_sample_wait_s) * 1000ULL;
    ESP_LOGI(
        kTag,
        "Waiting up to %" PRIu16 " s for the first bus-voltage sample (threshold %" PRIu16
        " mV, prior low-voltage sleeps %" PRIu32 ")",
        config.power_gate_sample_wait_s,
        config.power_gate_threshold_mv,
        decision.prior_sleeps);

    for (;;) {
        if (findPowerMonitorVoltage(
                sensor_config_list, measurement_store, decision.sensor_id, decision.voltage_mv)) {
            decision.has_voltage = true;
            break;
        }
        if (uptimeMilliseconds() >= deadline_ms) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(kSamplePollIntervalMs));
        // The calling task (app_main) is TWDT-subscribed; a reset failure here
        // is not actionable because the wait is bounded well under the timeout.
        static_cast<void>(esp_task_wdt_reset());
    }
    decision.wait_ms = static_cast<std::uint32_t>(uptimeMilliseconds() - started_ms);

    char detail[160];
    if (!decision.has_voltage) {
        if (reset_reason == ESP_RST_BROWNOUT) {
            requestSleep(config, decision, PowerGateOutcome::kSleepBrownout);
            std::snprintf(
                detail,
                sizeof(detail),
                "No bus-voltage sample within %" PRIu32 " ms after a brownout reset; sleeping %" PRIu32 " s",
                decision.wait_ms,
                decision.sleep_seconds);
            decision.detail = detail;
            ESP_LOGW(kTag, "%s", detail);
            return decision;
        }
        decision.outcome = PowerGateOutcome::kNoSample;
        std::snprintf(
            detail,
            sizeof(detail),
            "No bus-voltage sample within %" PRIu32 " ms; gate skipped, boot continues",
            decision.wait_ms);
        decision.detail = detail;
        ESP_LOGW(kTag, "%s", detail);
        g_rtc_low_voltage_sleeps = 0U;
        return decision;
    }

    if (decision.voltage_mv >= static_cast<float>(config.power_gate_threshold_mv)) {
        decision.outcome = PowerGateOutcome::kPassed;
        std::snprintf(
            detail,
            sizeof(detail),
            "Bus voltage %.0f mV from sensor #%" PRIu32 " is above the %" PRIu16 " mV threshold; boot continues",
            static_cast<double>(decision.voltage_mv),
            decision.sensor_id,
            config.power_gate_threshold_mv);
        decision.detail = detail;
        ESP_LOGI(kTag, "%s", detail);
        g_rtc_low_voltage_sleeps = 0U;
        return decision;
    }

    requestSleep(config, decision, PowerGateOutcome::kSleepLowVoltage);
    std::snprintf(
        detail,
        sizeof(detail),
        "Bus voltage %.0f mV from sensor #%" PRIu32 " is below the %" PRIu16
        " mV threshold; sleeping %" PRIu32 " s (low-voltage sleep #%" PRIu32 ")",
        static_cast<double>(decision.voltage_mv),
        decision.sensor_id,
        config.power_gate_threshold_mv,
        decision.sleep_seconds,
        decision.prior_sleeps + 1U);
    decision.detail = detail;
    ESP_LOGW(kTag, "%s", detail);
    return decision;
}

void PowerGate::enterDeepSleep(std::uint32_t seconds) {
    const std::uint64_t sleep_us = static_cast<std::uint64_t>(seconds) * 1000000ULL;
    const esp_err_t timer_err = esp_sleep_enable_timer_wakeup(sleep_us);
    if (timer_err != ESP_OK) {
        // Without a timer wake-up the device would sleep forever; fall back to
        // a reboot so the gate simply re-evaluates on the next boot.
        ESP_LOGE(
            kTag,
            "Failed to arm deep-sleep timer (%s); restarting instead",
            esp_err_to_name(timer_err));
        esp_restart();
    }
    ESP_LOGI(kTag, "Entering deep sleep for %" PRIu32 " s", seconds);
    esp_deep_sleep_start();
}

}  // namespace air360
