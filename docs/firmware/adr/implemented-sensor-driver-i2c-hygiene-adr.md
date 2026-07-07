# Sensor Driver I2C Hygiene ADR

## Status

Implemented.

Notes from implementation: SCD30 keeps its `record_` member (it reads it for forced-recalibration handling) and keeps its library-default descriptor config (it never hand-wrote the three-line block, so `applyDescriptorDefaults()` was not introduced there). INA219 previously set only the clock speed; it now also enables internal pull-ups via the shared helper, which is a deliberate consistency change. AHT30's `aht30_delete()` returns `void`, so it has nothing to log in teardown. The dead `record_` member in `mhz19b_sensor.hpp` (declared, never even assigned) was removed as part of step 3.

## Decision Summary

Consolidate four small duplication and consistency issues in the I2C sensor drivers, found during the BME280 migration review: centralise the per-device descriptor policy (clock speed, pull-ups) in one `I2cBusManager` helper, hoist the Pa→hPa conversion constant into the shared sensor types header, delete write-only `record_` members from drivers that never read them, and log teardown failures consistently across drivers.

None of these are runtime bugs today. They are drift points: each one encodes a single policy in multiple files, so a future change applied to some copies but not others produces inconsistent hardware or diagnostic behaviour that is hard to bisect.

## Context

The sensor runtime binds all I2C drivers through `I2cBusManager` (`firmware/main/src/sensors/transport_binding.cpp`), but per-device electrical configuration is applied in each driver individually. The BME280 migration to `esp-idf-lib/bmp280` added new copies of several existing patterns, which made the duplication visible:

1. **Descriptor policy.** The three lines `cfg.master.clk_speed = …; cfg.sda_pullup_en = 1; cfg.scl_pullup_en = 1;` now exist in at least eight places: `I2cBusManager::setupDevice()` (`transport_binding.cpp:72`), the lazy-install `probe_dev` in `getMasterBusHandle()`, and after `*_init_desc()` in `bme280_sensor.cpp:65`, `bme680_sensor.cpp`, `sht3x_sensor.cpp`, `sht4x_sensor.cpp`, `htu2x_sensor.cpp`, `veml7700_sensor.cpp`, `ina219_sensor.cpp`. The bus is installed by whichever descriptor touches the port first, so a policy change that misses one copy makes bus electrical config ordering-dependent.

2. **Pressure unit constant.** `constexpr float kPaPerHpa = 100.0F;` is defined identically (comment included) in `bme280_sensor.cpp:24` and `bmp390_sensor.cpp:20`. It encodes the unit contract of `SensorValueKind::kPressureHpa`, which belongs next to that enum, not in per-driver anonymous namespaces.

3. **Write-only `record_` members.** Several I2C drivers declare a `SensorRecord record_` member, assign it in `init()`, and never read it (`bme280_sensor.cpp:44`, `bmp390_sensor.cpp:34`, `sht3x_sensor.cpp` and others — verify the full list with a grep at implementation time). Only the DHT and GPS drivers actually read `record_` after `init()`. Dead state misleads readers into editing it expecting an effect.

4. **Teardown logging.** `bmp390_sensor.cpp` logs a failed `bmp390_delete()` with `ESP_LOGW`; `bme280_sensor.cpp:163` and the other esp-idf-lib drivers silently discard the `esp_err_t` from `*_free_desc()`. The firmware return-value rule permits the discard (no `[[nodiscard]]`), but the observability guidance ("return values that affect recovery should be logged") is applied inconsistently, and a stuck port mutex during teardown currently leaves no trace.

## Goals

- One author for I2C descriptor electrical policy.
- One definition of the hPa unit conversion, co-located with the value kind it serves.
- No write-only members in driver classes.
- Uniform, greppable teardown diagnostics across drivers.

## Non-Goals

- Changing any runtime behaviour visible to the measurement pipeline.
- Refactoring the driver interface (`SensorDriver`) or the binding paths themselves.
- Touching UART/one-wire drivers beyond the `record_` cleanup where applicable.

## Architectural Decision

### 1. `I2cBusManager::applyDescriptorDefaults()`

Add a small helper:

```cpp
// Apply the shared electrical policy (clock speed, internal pull-ups) to an
// i2cdev descriptor. The single author for per-device bus configuration.
void applyDescriptorDefaults(i2c_dev_t& dev, std::uint32_t speed_hz) const;
```

Use it from `setupDevice()`, from the `probe_dev` in `getMasterBusHandle()`, and in every driver currently hand-writing the three lines after `*_init_desc()`. Drivers keep their per-sensor `k…I2cSpeedHz` constants and pass them in.

### 2. Shared `kPaPerHpa`

Move the constant into `firmware/main/include/air360/sensors/sensor_types.hpp` next to `SensorValueKind` (e.g. `constexpr float kPaPerHpa = 100.0F;` with the unit-contract comment). Delete the per-driver copies in `bme280_sensor.cpp` and `bmp390_sensor.cpp`.

### 3. Remove write-only `record_` members

For each I2C driver whose `record_` is assigned but never read, delete the member and the assignment. Keep `record_` in DHT and GPS (NMEA), which read it during polling. Confirm the final list mechanically: `grep -n "record_" firmware/main/src/sensors/drivers/*.cpp` and keep only drivers with reads.

### 4. Teardown logging rule

Adopt the `bmp390` pattern as the norm: in `teardown()`, if a component release call (`*_free_desc()`, `*_delete()`) returns an error, log it with `ESP_LOGW` under the driver's `kTag`. Apply to all drivers whose release call returns `esp_err_t`. Record the rule in `docs/firmware/sensors/adding-new-sensor.md` so new drivers follow it.

## Affected Files

- `firmware/main/include/air360/sensors/transport_binding.hpp`, `firmware/main/src/sensors/transport_binding.cpp` — new helper, use in `setupDevice()` and `getMasterBusHandle()`
- `firmware/main/include/air360/sensors/sensor_types.hpp` — shared `kPaPerHpa`
- `firmware/main/src/sensors/drivers/`: `bme280_sensor.cpp`, `bme680_sensor.cpp`, `bmp390_sensor.cpp`, `sht3x_sensor.cpp`, `sht4x_sensor.cpp`, `htu2x_sensor.cpp`, `veml7700_sensor.cpp`, `ina219_sensor.cpp`, plus matching headers for the `record_` cleanup
- `docs/firmware/transport-binding.md` — document the helper in the Public API table
- `docs/firmware/sensors/adding-new-sensor.md` — descriptor-defaults helper and teardown-logging rule for new drivers

## Alternatives Considered

### Option A. Leave as-is

The copies are individually tiny. Rejected: the count is now ~8 and grows with every i2cdev driver; the failure mode (ordering-dependent bus config after a partial policy change) is disproportionately expensive to debug relative to the cost of a helper.

### Option B. Fold descriptor policy into each `*_init_desc()` wrapper

Rejected: the `*_init_desc()` functions live in third-party components; wrapping each one adds more surface than a single project-side helper.

### Option C. Single shared helper + shared constant + dead-state removal + logging rule (accepted)

Smallest change that leaves each policy with exactly one author.

## Practical Conclusion

This is a half-day cleanup with no behaviour change, best done as one commit so the pattern flips everywhere at once. After it, adding the ninth I2C driver copies three fewer things, and the next board revision's pull-up change touches exactly one function.
