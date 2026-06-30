# SCD30

## Status

Implemented. Keep this document aligned with the current SCD30 driver and registry defaults.

## Scope

This document covers the Air360 SCD30 driver, including default I2C binding, output fields, and noteworthy initialization behavior.

## Source of truth in code

- `firmware/main/src/sensors/drivers/scd30_sensor.cpp`
- `firmware/main/include/air360/sensors/drivers/scd30_sensor.hpp`
- `firmware/main/src/sensors/sensor_registry.cpp`

## Read next

- [README.md](README.md)
- [supported-sensors.md](supported-sensors.md)
- [../transport-binding.md](../transport-binding.md)

Optical CO₂ sensor (NDIR) with temperature and humidity from Sensirion.

## Transport

- I2C, bus 0 (SDA=GPIO8, SCL=GPIO9)
- Address: `0x61` (fixed)

## Initialization

1. Resolve bus pins via `context.i2c_bus_manager->resolvePins()`
2. Initialize the sensor descriptor via `scd30_init_desc()` (passes resolved port and GPIO pins)
3. Verify device presence via `i2c_dev_check_present()`
4. Calculate the measurement interval from `poll_interval_ms`:
   ```
   interval_s = (poll_interval_ms + 999) / 1000
   interval_s = clamp(interval_s, 3, 1799)
   ```
5. Apply the interval via `scd30_set_measurement_interval()` — forced to the fastest accepted rate (3 s) instead when an FRC maintenance action is armed (see [Forced recalibration](#forced-recalibration-frc))
6. Start continuous measurement via `scd30_trigger_continuous_measurement(altitude=0)`
7. Apply the configured automatic self-calibration (ASC) state via `scd30_set_automatic_self_calibration()` from `SensorRecord::startup_calibration` (see [Automatic self-calibration](#automatic-self-calibration-asc))
8. Arm the forced-recalibration (FRC) maintenance action when `SensorRecord::pending_maintenance_action` requests it (see [Forced recalibration](#forced-recalibration-frc))

After initialization `last_error_` is set to `"Waiting for first SCD30 sample."` — this is expected, not a fault condition.

## Polling

Each poll cycle:
1. Query data-ready status via `scd30_get_data_ready_status()`
2. If not ready: return `ESP_OK` with `"Waiting for new SCD30 sample."` in the error field — the measurement is skipped but the driver stays initialized
3. If ready: read CO₂, temperature, and humidity via `scd30_read_measurement()`
4. Validate that no NaN values are returned

## Measurements

| Measurement | ValueKind | Unit |
|-------------|-----------|------|
| CO₂ | `kCo2Ppm` | ppm |
| Temperature | `kTemperatureC` | °C |
| Humidity | `kHumidityPercent` | % |

## Notes

- The sensor operates in **continuous measurement mode** — it generates readings autonomously at the configured interval. The driver polls a data-ready flag and reads only when fresh data is available. At short `poll_interval_ms` values some polls will return no data.
- The measurement interval is derived from `poll_interval_ms` at initialization time. Changing the poll interval through the web UI requires a sensor manager restart (Apply button).
- Altitude is hardcoded to 0 m. The SCD30 can use altitude for pressure compensation — for accurate CO₂ readings at significant elevation this would require a code change.
- The first few readings after power-on may be unstable. NDIR sensors require a warm-up period.
- If reading the data-ready flag or the measurement itself returns an error, `initialized_` is reset.

## Automatic self-calibration (ASC)

The SCD30 is the first sensor to use the generic per-sensor `startup_calibration` flag (see [configuration-reference.md](../configuration-reference.md#sensorrecord-fields) and [nvs.md](../nvs.md)). Its descriptor sets `supports_startup_calibration = true` with the UI label "Automatic self-calibration (ASC)", so the web UI shows a calibration checkbox on the SCD30 card.

Behavior:

- When the checkbox is enabled, `init()` calls `scd30_set_automatic_self_calibration(dev, true)`; when disabled it calls it with `false`. The state is re-asserted on every `init()`/re-init, which is idempotent and keeps the firmware config authoritative even if the sensor is swapped.
- A failure to set ASC is **non-fatal**: it is logged as a warning and initialization continues, so the sensor still measures (just without the requested ASC state).
- ASC is the recommended mode for **permanently powered outdoor units with regular fresh-air exposure** (~400 ppm baseline). It needs roughly **7 days of continuous operation** to converge, and it calibrates incorrectly in environments that never return to outdoor CO₂ levels (e.g. greenhouses, continuously occupied rooms) — use FRC there instead.
- ASC (a persistent mode) and FRC (a one-shot action) are **not** mutually exclusive: you can schedule an FRC for the next boot and still run with ASC enabled afterwards. FRC supersedes earlier ASC corrections and is wired through the separate one-shot maintenance-action mechanism described below, so it does not re-run on every boot.

## Forced recalibration (FRC)

The SCD30 advertises a one-shot **forced recalibration** maintenance action (`MaintenanceActionKind::kForcedRecalibration`, UI key `frc`). It maps to `SensorRecord::pending_maintenance_action` and runs once after the next boot via the shared mechanism in [maintenance-actions.md](maintenance-actions.md). Use it to immediately correct sensor drift when a known reference concentration is available, rather than waiting ~7 days for ASC to converge.

Behavior:

- When armed, `init()` forces the measurement interval to **3 s** (instead of the poll-derived interval) — the datasheet asks for a ~2 s rate before FRC, but the esp-idf-lib driver rejects intervals ≤ 2 s (`CHECK_ARG: interval > 2`), so 3 s is the fastest accepted floor.
- The state machine in `poll()` waits for **≥ 120 s elapsed** *and* **≥ 20 fresh samples**, then issues `scd30_set_forced_recalibration_value(dev, 400)` against a **400 ppm fresh-air reference** (matching the outdoor-unit use case). The reference is fixed in firmware.
- On success the action reports `kCompleted` ("FRC complete (400 ppm reference)"); on command error or a **300 s timeout** without enough samples it reports `kFailed`. Either terminal state restores the configured measurement interval and is non-fatal — the sensor keeps measuring.
- `SensorManager` then clears `pending_maintenance_action` and re-saves the config, so FRC does not re-run. A reboot mid-warm-up simply restarts it (at-least-once).
- **Operator note:** place the unit in stable fresh outdoor air (~400 ppm) before the FRC boot. Recalibrating against a non-400 ppm environment will bias all subsequent readings.

## Recommended poll interval

Minimum 5 seconds (registry limit). The SCD30 supports intervals from 2 to 1800 seconds — the driver ensures a value of 3–1799 s after conversion.

## Component

`esp-idf-lib__scd30` (managed component)

## Source files

- `firmware/main/src/sensors/drivers/scd30_sensor.cpp`
- `firmware/main/include/air360/sensors/drivers/scd30_sensor.hpp`
