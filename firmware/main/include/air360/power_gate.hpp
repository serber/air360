#pragma once

#include <cstdint>
#include <string>

#include "air360/config_repository.hpp"
#include "air360/sensors/sensor_config.hpp"
#include "air360/uploads/measurement_store.hpp"
#include "esp_system.h"

namespace air360 {

// Result of the boot-time power gate. The gate runs after the kBeforeNetwork
// sensors (INA219/INA226) have started and before any radio is powered.
enum class PowerGateOutcome : std::uint8_t {
    kDisabled = 0U,      // power_gate_enabled == 0
    kNoPowerMonitor,     // enabled, but no enabled INA219/INA226 in the sensor list
    kNoSample,           // enabled, monitor present, no reading within the wait window
    kPassed,             // bus voltage at or above the threshold
    kSleepLowVoltage,    // bus voltage below the threshold; deep sleep requested
    kSleepBrownout,      // no reading and the last reset was a brownout; deep sleep requested
};

const char* powerGateOutcomeKey(PowerGateOutcome outcome);

struct PowerGateDecision {
    PowerGateOutcome outcome = PowerGateOutcome::kDisabled;
    bool enabled = false;
    // True when App must call PowerGate::enterDeepSleep(sleep_seconds) instead
    // of continuing boot.
    bool sleep_requested = false;
    std::uint32_t sleep_seconds = 0U;
    std::uint16_t threshold_mv = 0U;
    bool has_voltage = false;
    float voltage_mv = 0.0F;
    std::uint32_t sensor_id = 0U;       // power monitor that produced the reading
    std::uint32_t wait_ms = 0U;         // how long boot waited for the first reading
    // Consecutive low-power sleeps that preceded this boot (RTC-retained), and
    // the duration of the most recent one. Both are 0 after a cold boot.
    std::uint32_t prior_sleeps = 0U;
    std::uint32_t last_sleep_seconds = 0U;
    std::string detail;                 // one-line human-readable summary
};

// Boot-time power gate. Compares the first INA bus-voltage sample against the
// configured threshold and asks App to deep-sleep while the supply is too weak
// to carry the radios. Escalates the sleep duration (base, x2, x4, ... capped)
// across consecutive low-voltage wake-ups using RTC-retained counters.
//
// Fail-open by design: a disabled gate, a missing power monitor, or a monitor
// that produces no reading within power_gate_sample_wait_s lets boot continue.
class PowerGate {
  public:
    // Blocks for at most config.power_gate_sample_wait_s seconds while waiting
    // for the first voltage sample, feeding the TWDT from the calling task.
    // Never sleeps by itself; the caller acts on sleep_requested.
    PowerGateDecision evaluate(
        const DeviceConfig& config,
        const SensorConfigList& sensor_config_list,
        const MeasurementStore& measurement_store,
        esp_reset_reason_t reset_reason);

    // Arms the RTC timer wake-up and enters deep sleep. Does not return; the
    // next boot starts from the bootloader with RTC memory retained.
    [[noreturn]] static void enterDeepSleep(std::uint32_t seconds);
};

}  // namespace air360
