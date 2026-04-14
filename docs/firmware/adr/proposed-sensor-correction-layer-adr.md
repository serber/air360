# Sensor Correction Layer ADR

## Status

Proposed.

## Decision Summary

Add a unified per-sensor correction layer for climate-oriented measurements so Air360 can apply offsets consistently across supported sensor families instead of leaving correction behavior fragmented or sensor-specific.

The first version should support:

- temperature offsets
- humidity offsets
- pressure offsets where the sensor reports pressure

## Context

Air360 already supports multiple climate sensors:

- `BME280`
- `BME680`
- `HTU2X`
- `SHT4X`
- `SCD30`
- `DHT11`
- `DHT22`

The current configuration model has no generic place to store measurement corrections. This overlaps directly with recurring Sensor.Community complaints:

- [#1044 Temperature correction missing for some sensors](https://github.com/opendata-stuttgart/sensors-software/issues/1044)
- [#1033 BME280 temperature too high](https://github.com/opendata-stuttgart/sensors-software/issues/1033)
- [#927 Temperature Correction Doesn't Seem To Work](https://github.com/opendata-stuttgart/sensors-software/issues/927)
- [#979 temp compensation limited? unable to apply with HTU21D](https://github.com/opendata-stuttgart/sensors-software/issues/979)
- [#687 Korrekturfaktor Temperatur](https://github.com/opendata-stuttgart/sensors-software/issues/687)
- [PR #1048 enable IIR filtering for BME280](https://github.com/opendata-stuttgart/sensors-software/pull/1048)

Users do not experience this as a driver-level issue. They experience it as “the device reads too warm”, “the humidity is off”, or “correction works on one sensor family but not on another”.

## Goals

- Provide one correction model that works across all supported climate sensors.
- Keep the correction logic outside individual drivers where possible.
- Make corrected values visible consistently in the UI, `/status`, and uploads.
- Keep room for raw diagnostic values when needed.

## Non-Goals

- Full calibration workflows for every sensor family.
- Sensor-specific multi-point calibration curves in the first version.
- Correcting PM, GPS, or analog gas values in this ADR.

## Architectural Decision

### 1. Add correction fields to the persisted sensor record

Extend `SensorRecord` with optional offsets for:

- `temperature_offset_c`
- `humidity_offset_percent`
- `pressure_offset_hpa`

Each field should default to zero and be applied only when the sensor type actually emits that measurement kind.

### 2. Apply corrections in one common post-processing step

Do not copy correction math into every sensor driver.

The preferred model is:

1. driver reads raw values
2. sensor runtime builds the `Measurement`
3. a shared correction pass adjusts matching value kinds based on the configured sensor record

This keeps driver code simpler and avoids repeating the same mistake across multiple implementations.

### 3. Keep runtime diagnostics honest

The user-facing runtime should clearly indicate whether a value is corrected.

The first version may choose either:

- show corrected values everywhere and expose raw values only in debug output
- or show corrected values normally with an optional raw diagnostics section

What should be avoided is mixed behavior where some surfaces use corrected values and others use raw values.

### 4. Fold low-risk sensor tuneables into the same design pass

Some user pain is not a pure offset problem. `BME280` filtering and measurement mode choices also affect perceived quality.

This ADR keeps room for:

- configurable `BME280` IIR filter setting
- future low-noise tuneables that belong to the same “make climate data believable” story

## Affected Files

- `firmware/main/include/air360/sensors/sensor_config.hpp`
- `firmware/main/src/sensors/sensor_config_repository.cpp`
- `firmware/main/src/sensors/sensor_manager.cpp`
- `firmware/main/src/web_ui.cpp`
- `firmware/main/src/web_server.cpp`
- `firmware/main/src/status_service.cpp`
- relevant firmware docs under `docs/firmware/`

## Alternatives Considered

### Option A. Driver-specific ad hoc correction

Simple to start, but repeats logic across drivers and guarantees inconsistent behavior between sensor families.

### Option B. Shared correction layer on top of sensor output (accepted)

Keeps the model consistent and makes the UI/config story much cleaner.

### Option C. No correction support

Rejected. The ecosystem repeatedly shows that raw climate readings are often not acceptable in enclosure-based field installations.

## Reference Links

- [Sensor.Community ecosystem review for Air360](../../ecosystem/sensor-community-issues-prs-review-2026-04-14.md)
- [Configuration Reference](../configuration-reference.md)
- [Sensors index](../sensors/README.md)
- [BME280](../sensors/bme280.md)
- [SCD30](../sensors/scd30.md)
- [#1044 Temperature correction missing for some sensors](https://github.com/opendata-stuttgart/sensors-software/issues/1044)
- [#1033 BME280 temperature too high](https://github.com/opendata-stuttgart/sensors-software/issues/1033)
- [#927 Temperature Correction Doesn't Seem To Work](https://github.com/opendata-stuttgart/sensors-software/issues/927)
- [#979 temp compensation limited? unable to apply with HTU21D](https://github.com/opendata-stuttgart/sensors-software/issues/979)
- [#687 Korrekturfaktor Temperatur](https://github.com/opendata-stuttgart/sensors-software/issues/687)
- [PR #1048 enable IIR filtering for BME280](https://github.com/opendata-stuttgart/sensors-software/pull/1048)

## Practical Conclusion

Air360 should stop treating climate correction as a driver quirk and treat it as a first-class sensor configuration feature. A shared correction layer closes repeated user pain with relatively low architectural risk.
