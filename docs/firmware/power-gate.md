# Power Gate

## Status

Implemented. Keep this document aligned with the current `firmware/` tree.

## Scope

This document covers the boot-time power gate: how the firmware decides, before any radio is powered, whether the supply can carry the modem, Wi-Fi, BLE, and the remaining sensors, and how it deep-sleeps with an escalating timer when it cannot. Runtime (post-boot) low-voltage handling is out of scope; the gate runs once per boot.

## Source of truth in code

- `firmware/main/src/power_gate.cpp`
- `firmware/main/include/air360/power_gate.hpp`
- `firmware/main/src/app.cpp` (`App::bootPowerGate`)
- `firmware/main/src/config_repository.cpp` (power-gate fields, defaults, validation)
- `firmware/main/src/status_service.cpp` (`power_gate` JSON object and overview row)

## Read next

- [startup-pipeline.md](startup-pipeline.md)
- [configuration-reference.md](configuration-reference.md)
- [sensors/ina226.md](sensors/ina226.md)
- [network-manager.md](network-manager.md)

---

## Why it exists

On a solar build the morning is the hard case: the pack sat at the BMS cutoff all night, the panel now delivers a trickle, and the ESP32-S3 boots. The old boot order lit every sensor, the modem, and Wi-Fi within seconds. Wi-Fi TX peaks then pulled the rail below brownout, the chip reset, and the cycle repeated without ever letting the pack charge.

The power gate breaks that loop. Boot step 5 starts only the `kBeforeNetwork` sensors (INA219/INA226, see [startup-pipeline.md](startup-pipeline.md#sensor-startup-phases)), boot step 7 reads their bus voltage while the supply is still lightly loaded, and if it is below the configured threshold the device goes back to deep sleep instead of powering the radios.

---

## Decision flow

```text
power_gate_enabled == 0 ──────────────────────────► kDisabled        → continue
no enabled INA219/INA226 in sensor_cfg ───────────► kNoPowerMonitor  → continue
prior consecutive sleeps ≥ 4 ─────────────────────► kBypassedAfterSleeps → continue (escape hatch)
wait ≤ power_gate_sample_wait_s for kVoltageMv
  ├─ no sample, reset reason == BROWNOUT ─────────► kSleepBrownout   → deep sleep
  ├─ no sample, any other reset ──────────────────► kNoSample        → continue
  ├─ voltage ≥ power_gate_threshold_mv ───────────► kPassed          → continue
  └─ voltage <  power_gate_threshold_mv ───────────► kSleepLowVoltage → deep sleep
```

The gate is **fail-open**: every path that cannot prove the supply is weak lets boot continue. A missing or broken power monitor never bricks the station.

### Escape hatch

The gate runs at boot step 7, before Wi-Fi (step 9) and the web server (step 12). A threshold set above the voltage the supply can actually reach would therefore put the device to sleep on every boot with no way to open `/config`. To keep the operator in control, `PowerGate::evaluate()` lets one boot through unconditionally once the RTC counter reports `kMaxConsecutiveGateSleeps` (4) consecutive low-voltage sleeps — with the default durations that is after 5 + 10 + 20 + 30 minutes. The outcome is `kBypassedAfterSleeps`, the Overview row shows a red **Bypassed** chip with "check the threshold", and the sleep counter is zeroed. If the supply really is too weak, that boot browns out, the counters reset on the non-deep-sleep reset, and the escalation restarts from the base duration; the cost is one full boot attempt per chain, roughly once an hour.

### Waiting for the first sample

`PowerGate::evaluate()` polls `MeasurementStore::runtimeInfoForSensor()` every 250 ms for the first enabled power-monitor record whose latest measurement carries `SensorValueKind::kVoltageMv`. Warmup readings count — the sensor task records them into the latest-measurement snapshot even though they are not queued for upload. The wait is bounded by `power_gate_sample_wait_s` (default 15 s, range 5–120 s) and feeds the TWDT from the main task on every iteration.

### What the voltage means

The comparison is against the INA **bus voltage**, so the threshold depends on where the module is wired:

| INA placement | What the bus voltage tracks | Typical threshold |
|---------------|-----------------------------|-------------------|
| Shield 5 V input path (default assembly) | LM2596 output; sags to ~4.5–4.8 V when the 2S LiFePO4 pack drops near 6.2–6.5 V | `4700` mV (default) |
| Battery side (INA226, up to 36 V) | Pack voltage directly | ~`6200` mV for 2S LiFePO4 |

The reading is taken with no radio running, so it is the pack's lightly-loaded voltage. Set the threshold to the level at which the device is known to survive a Wi-Fi join.

---

## Deep sleep and escalation

When the gate requests sleep, `App::bootPowerGate()`:

1. stores the decision in `StatusService` (irrelevant for this boot, but keeps the code path uniform),
2. logs the reason,
3. turns the RGB LED off,
4. calls `PowerGate::enterDeepSleep(seconds)`, which arms `esp_sleep_enable_timer_wakeup()` and calls `esp_deep_sleep_start()`.

Nothing else has been started at that point: the modem, Wi-Fi, BLE, uploads, and the web server all come later in the boot order, and the only running task besides `app_main` is `air360_sensor` driving the power monitor. Deep sleep drops all RAM state; the next boot starts from the bootloader.

If arming the wake-up timer fails, the firmware calls `esp_restart()` instead of sleeping without a wake source.

### Escalating sleep duration

Two counters live in RTC slow memory (`RTC_DATA_ATTR`), which survives deep sleep but not a cold boot:

| Counter | Meaning |
|---------|---------|
| `g_rtc_low_voltage_sleeps` | Consecutive low-voltage sleeps before this boot |
| `g_rtc_last_sleep_seconds` | Duration of the most recent sleep |

The sleep duration is `power_gate_sleep_base_s × 2^n`, capped at `power_gate_sleep_max_s`, where `n` is the number of prior consecutive sleeps. With the defaults (300 s base, 1800 s cap) the sequence is 5 → 10 → 20 → 30 → 30 → … minutes. The counters are trusted only when `esp_reset_reason() == ESP_RST_DEEPSLEEP`; any other reset (power-on, brownout, software, watchdog) zeroes them so the chain restarts from the base duration. A boot that passes, skips, or bypasses the gate also zeroes the sleep counter.

### Brownout fallback

If the last reset was a brownout **and** no voltage sample arrived within the wait window, the gate sleeps for the escalated duration anyway (`kSleepBrownout`). This covers a power monitor that itself failed to answer on a sagging rail. Without a brownout the same missing sample only logs a warning and boot continues.

---

## Configuration

All settings live in `DeviceConfig` and are edited on the `/config` page in the "Power gate (INA)" card, which is rendered only while an INA219 or INA226 is in the sensor list. Field ranges and defaults are in [configuration-reference.md](configuration-reference.md#device-configuration-device_cfg); the NVS layout is in [nvs.md](nvs.md#device_cfg--deviceconfig).

| Field | Default | Effect |
|-------|---------|--------|
| `power_gate_enabled` | `0` | Master switch |
| `power_gate_threshold_mv` | `4700` | Boot continues only at or above this bus voltage |
| `power_gate_sample_wait_s` | `15` | Longest wait for the first INA reading |
| `power_gate_sleep_base_s` | `300` | First sleep duration |
| `power_gate_sleep_max_s` | `1800` | Cap for the doubling sleep duration |

---

## Observability

- **Serial log** (`air360.power_gate`): one line per decision, e.g. `Bus voltage 4520 mV from sensor #3 is below the 4700 mV threshold; sleeping 300 s (low-voltage sleep #1)`.
- **Overview page**: a "Power gate" row in the System card (only while the gate is enabled) shows Passed/Skipped with the measured voltage and, after a sleep chain, how many low-voltage sleeps preceded this boot.
- **Status JSON** (`/diagnostics` raw dump): a top-level `power_gate` object:

```json
"power_gate": {
  "enabled": true,
  "outcome": "passed",
  "threshold_mv": 4700,
  "has_voltage": true,
  "voltage_mv": 4985,
  "sensor_id": 3,
  "wait_ms": 5210,
  "prior_sleeps": 2,
  "last_sleep_s": 600,
  "detail": "Bus voltage 4985 mV from sensor #3 is above the 4700 mV threshold; boot continues"
}
```

`outcome` is one of `disabled`, `no_power_monitor`, `no_sample`, `passed`, `sleep_low_voltage`, `sleep_brownout`, `bypassed_after_sleeps`. The two `sleep_*` values are never visible on a live page, because the device sleeps instead of serving it; they appear only in the serial log.

---

## Limitations

- The gate runs once per boot. A supply that collapses after Wi-Fi is up is still handled only by the brownout detector and the next boot.
- Deep sleep powers down the ESP32-S3 only. A SIM7600 module that was left powered by a previous session, the LM2596 quiescent current, and any sensor with its own regulator keep drawing from the pack; the gate removes the radio peaks, not the whole load.
- RTC counters are lost on a cold boot, so a BMS cutoff between sleeps restarts the escalation from the base duration.
- A threshold above the real supply voltage is not detected as such; the escape hatch only guarantees a full boot every four sleeps so the value can be corrected from `/config`.
