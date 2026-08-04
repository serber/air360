# SCD40 / SCD41

## Status

Implemented. Keep this document aligned with the current SCD4x driver and registry defaults.

## Scope

This document covers the Air360 SCD40 and SCD41 driver, including default I2C binding, output fields, and noteworthy initialization behavior. Both sensors share one driver (`Scd4xSensor`, parameterized by `Scd4xModel`) because they use the same `esp-idf-lib/scd4x` component and protocol; SCD41 additionally supports an on-demand single-shot mode that this driver does not use.

## Source of truth in code

- `firmware/main/src/sensors/drivers/scd4x_sensor.cpp`
- `firmware/main/include/air360/sensors/drivers/scd4x_sensor.hpp`
- `firmware/main/src/sensors/sensor_registry.cpp`

## Read next

- [README.md](README.md)
- [supported-sensors.md](supported-sensors.md)
- [../transport-binding.md](../transport-binding.md)

Photoacoustic NDIR CO₂ sensor with temperature and humidity from Sensirion. SCD41 is a smaller, single-shot-capable variant of SCD40; both expose an identical register-level protocol for periodic measurement, ASC, and FRC.

## Transport

- I2C, bus 0 (SDA=GPIO8, SCL=GPIO9)
- Address: `0x62` (fixed, identical for both models)

## Initialization

1. Resolve bus pins via `context.i2c_bus_manager->resolvePins()`
2. Initialize the sensor descriptor via `scd4x_init_desc()`, then apply the shared electrical policy via `context.i2c_bus_manager->applyDescriptorDefaults()`
3. Verify device presence via `i2c_dev_check_present()`
4. Best-effort `scd4x_stop_periodic_measurement()` — most SCD4x commands (including ASC and FRC) NACK while a measurement is running, so the driver forces idle state before configuring. This is expected to be a no-op on a sensor that was already idle.
5. Apply the configured automatic self-calibration (ASC) state via `scd4x_set_automatic_self_calibration()` from `SensorRecord::startup_calibration` (see [Automatic self-calibration](#automatic-self-calibration-asc))
6. Start measurement:
   - If a forced-recalibration (FRC) maintenance action is armed, start the fast **regular** periodic measurement mode (`scd4x_start_periodic_measurement()`, ~5 s internal cadence) to shorten the required warm-up (see [Forced recalibration](#forced-recalibration-frc))
   - Otherwise start the **low-power** periodic measurement mode (`scd4x_start_low_power_periodic_measurement()`, ~30 s internal cadence), which matches the registry's 30 s minimum poll interval and draws less power for the permanently-powered outdoor-unit use case
7. Arm the FRC maintenance action when `SensorRecord::pending_maintenance_action` requests it

After initialization `last_error_` is set to `"Waiting for first SCD4x sample."` — this is expected, not a fault condition.

## Polling

Each poll cycle:
1. Query data-ready status via `scd4x_get_data_ready_status()`
2. If not ready: return `ESP_OK` with `"Waiting for new SCD4x sample."` in the error field — the measurement is skipped but the driver stays initialized
3. If ready: read CO₂, temperature, and humidity via `scd4x_read_measurement()`
4. Treat a CO₂ reading of exactly `0` ppm as an invalid/not-yet-valid sample (the sensor's own sentinel during the first seconds after starting periodic measurement) rather than a real-world reading — skipped the same way as "not ready"

## Measurements

| Measurement | ValueKind | Unit |
|-------------|-----------|------|
| CO₂ | `kCo2Ppm` | ppm |
| Temperature | `kTemperatureC` | °C |
| Humidity | `kHumidityPercent` | % |

## Notes

- The sensor operates in **periodic measurement mode** — it generates readings autonomously (every ~5 s in regular mode, ~30 s in low-power mode) and the driver polls a data-ready flag, reading only when fresh data is available.
- Altitude and ambient-pressure compensation (`scd4x_set_sensor_altitude()` / `scd4x_set_ambient_pressure()`) and the temperature-offset command are not used — the sensor runs with its factory defaults. This is a known limitation matching the SCD30 driver's altitude handling.
- The first few readings after starting periodic measurement return CO₂ = 0 while the sensor stabilizes; the driver discards these silently instead of treating them as poll failures.
- If reading the data-ready flag or the measurement itself returns an I2C error, `initialized_` is reset via the shared soft-fail/re-init policy.

## Automatic self-calibration (ASC)

Uses the same generic per-sensor `startup_calibration` flag as SCD30 (see [configuration-reference.md](../configuration-reference.md#sensorrecord-fields) and [nvs.md](../nvs.md)). Both descriptors set `supports_startup_calibration = true` with the UI label "Automatic self-calibration (ASC)".

Behavior:

- When the checkbox is enabled, `init()` calls `scd4x_set_automatic_self_calibration(dev, true)`; when disabled it calls it with `false`. The state is re-asserted on every `init()`/re-init, which is idempotent.
- A failure to set ASC is **non-fatal**: it is logged as a warning and initialization continues.
- ASC is the recommended mode for **permanently powered outdoor units with regular fresh-air exposure** (~400 ppm baseline). Per Sensirion, it needs several days of continuous operation to converge, and it calibrates incorrectly in environments that never return to outdoor CO₂ levels — use FRC there instead.
- ASC and FRC are **not** mutually exclusive: you can schedule an FRC for the next boot and still run with ASC enabled afterwards.

## Forced recalibration (FRC)

Both SCD40 and SCD41 advertise a one-shot **forced recalibration** maintenance action (`MaintenanceActionKind::kForcedRecalibration`, UI key `frc`), run once after the next boot via the shared mechanism in [maintenance-actions.md](maintenance-actions.md).

Behavior:

- When armed, `init()` starts the fast **regular** periodic measurement mode (~5 s cadence) instead of low-power mode, per the Sensirion datasheet's requirement to warm up in periodic measurement before issuing FRC.
- The state machine in `poll()` waits for **≥ 180 s elapsed** *and* **≥ 30 fresh samples**, then:
  1. Stops periodic measurement via `scd4x_stop_periodic_measurement()` — SCD4x, unlike SCD30, requires the sensor to be idle before FRC.
  2. Issues `scd4x_perform_forced_recalibration(dev, 400, &frc_correction)` against a **400 ppm fresh-air reference** (fixed in firmware).
  3. Treats a returned correction value of `0xFFFF` (Sensirion's documented failure sentinel) as a rejected calibration.
  4. On success, calls `scd4x_persist_settings()` so the correction survives a sensor power cycle — this is only done on an actual calibration event, not every boot, to respect the sensor's limited EEPROM write endurance.
  5. Unconditionally resumes low-power periodic measurement via `scd4x_start_low_power_periodic_measurement()` so normal readings continue regardless of the FRC outcome.
- On success the action reports `kCompleted` ("FRC complete (400 ppm reference)"); on command error, a rejected correction, or a **360 s timeout** without enough samples it reports `kFailed`. Either terminal state is non-fatal — the sensor keeps measuring.
- If resuming low-power measurement after FRC fails, the driver marks itself uninitialized so the manager re-runs `init()` with the normal backoff policy.
- `SensorManager` then clears `pending_maintenance_action` and re-saves the config, so FRC does not re-run. A reboot mid-warm-up simply restarts it (at-least-once).
- **Operator note:** place the unit in stable fresh outdoor air (~400 ppm) before the FRC boot. Recalibrating against a non-400 ppm environment will bias all subsequent readings.

## Recommended poll interval

30 seconds (registry minimum), matching the sensor's low-power internal cadence. Configuring a longer interval simply means the driver skips intermediate sensor-internal samples between manager polls.

## Component

`esp-idf-lib__scd4x` (managed component)

## Source files

- `firmware/main/src/sensors/drivers/scd4x_sensor.cpp`
- `firmware/main/include/air360/sensors/drivers/scd4x_sensor.hpp`
