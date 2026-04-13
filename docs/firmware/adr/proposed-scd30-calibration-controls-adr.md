# SCD30 Calibration Controls ADR

## Status

Proposed.

## Decision Summary

Expose calibration and compensation controls for the `SCD30` so the sensor can be configured intentionally instead of running with hidden fixed assumptions.

The first version should include:

- automatic self-calibration enable or disable
- altitude or ambient-pressure compensation input
- visible calibration-related runtime status

## Context

Air360 already supports `SCD30`, but the current behavior remains rigid:

- continuous measurement is started automatically
- altitude is hardcoded to `0 m`
- there is no user-facing calibration control

That directly maps to an upstream user question:

- [#1058 How "SCD30 Auto Calibration" activate](https://github.com/opendata-stuttgart/sensors-software/issues/1058)

For `CO2` sensors, hidden calibration assumptions are operationally expensive. They affect trust in the device more than many other sensor details.

## Goals

- Allow users to control `SCD30` automatic self-calibration.
- Allow users to provide compensation input for non-sea-level installations.
- Surface enough status that “waiting”, “warming up”, and “misconfigured” are distinguishable.

## Non-Goals

- Full laboratory-grade multi-point CO2 calibration workflow.
- Supporting every calibration feature exposed by the vendor library in the first version.
- Reworking other `CO2` sensor families in this ADR.

## Architectural Decision

### 1. Add SCD30-specific configuration fields

Extend the sensor configuration model for `SCD30` with:

- `auto_calibration_enabled`
- `compensation_mode`
- `compensation_altitude_m` or ambient pressure equivalent

These fields should be ignored by non-`SCD30` sensors.

### 2. Apply calibration settings during driver initialization

When `SCD30` initializes, configure:

- measurement interval
- self-calibration mode
- compensation input

This keeps the runtime model predictable and aligns with the current “save/apply sensor config, restart sensor runtime” design.

### 3. Improve runtime messaging

The current driver already reports “Waiting for first SCD30 sample.” Expand that approach so the runtime can distinguish:

- initial warm-up
- waiting for next sample
- calibration mode active
- invalid compensation configuration

### 4. Keep forced recalibration as an optional follow-up

Forced recalibration is useful, but it has more UX and safety implications than a simple toggle.

It should remain a follow-up once the baseline configuration surface exists.

## Affected Files

- `firmware/main/include/air360/sensors/sensor_config.hpp`
- `firmware/main/src/sensors/sensor_config_repository.cpp`
- `firmware/main/src/sensors/drivers/scd30_sensor.cpp`
- `firmware/main/src/web_ui.cpp`
- `firmware/main/src/status_service.cpp`
- related firmware docs

## Alternatives Considered

### Option A. Leave SCD30 calibration hidden in code

Simple for developers, weak for real deployments, and repeatedly confusing for users.

### Option B. Expose a small, explicit control surface (accepted)

Solves the real operational pain while keeping the first version tractable.

### Option C. Full advanced calibration UI immediately

Too much complexity for the first iteration.

## Reference Links

- [Sensor.Community ecosystem review for Air360](../../ecosystem/sensor-community-issues-prs-review-2026-04-14.md)
- [SCD30](../sensors/scd30.md)
- [Configuration Reference](../configuration-reference.md)
- [Live sensor reconfiguration ADR](implemented-live-sensor-reconfiguration-adr.md)
- [#1058 How "SCD30 Auto Calibration" activate](https://github.com/opendata-stuttgart/sensors-software/issues/1058)

## Practical Conclusion

Air360 should stop hardcoding the most important `SCD30` calibration assumptions. A small explicit control surface gives users predictable CO2 behavior and matches how serious deployments expect this class of sensor to work.
