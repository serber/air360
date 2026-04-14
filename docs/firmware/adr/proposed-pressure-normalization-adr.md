# Pressure Normalization ADR

## Status

Proposed.

## Decision Summary

Add explicit support for sea-level pressure normalization so Air360 can present and upload pressure values that are meaningful for real installations instead of exposing only raw station pressure.

The first version should support:

- installation altitude as configuration
- optional sea-level-normalized pressure derived from raw pressure
- clear labeling of raw versus normalized pressure

## Context

Pressure confusion is a long-running ecosystem problem:

- [#102 BME280 pressure shows wrong values](https://github.com/opendata-stuttgart/sensors-software/issues/102)
- [#71 Einheitendiskrepanz Luftdruck zwischen luftdaten und sensemap](https://github.com/opendata-stuttgart/sensors-software/issues/71)
- [#936 Wrong format of 'pressure value' of a BMP280 Sensor](https://github.com/opendata-stuttgart/sensors-software/issues/936)

The device usually reports absolute local pressure. Users then compare that number with weather maps or third-party dashboards that show sea-level pressure and conclude the sensor is wrong.

Air360 already has two useful building blocks:

- pressure-producing sensors in the climate sensor family
- `GPS (NMEA)` support with altitude in the measurement model

## Goals

- Let users enter installation altitude once.
- Compute sea-level-normalized pressure from raw pressure.
- Keep raw pressure available for diagnostics and advanced use.
- Make the displayed and uploaded semantics explicit.

## Non-Goals

- Full meteorological station feature parity.
- Automatic barometric trend analysis.
- Mandatory GPS-derived altitude in the first version.

## Architectural Decision

### 1. Keep raw pressure as the sensor truth

The sensor driver should continue to emit the raw measured pressure from hardware.

Normalization should happen later as a derived value, not by mutating the raw sensor read path.

### 2. Add installation altitude as configuration

Add a device-level or sensor-level altitude field in meters above sea level.

The field should:

- default to zero
- be optional
- be used only when the measurement includes pressure

### 3. Derive normalized pressure in a shared post-processing step

Once a pressure value is present and altitude is known, derive sea-level pressure with a deterministic formula and add it as a separate value kind or as a clearly labeled alternate presentation.

The key requirement is clarity:

- raw station pressure should stay available
- normalized pressure should not silently replace raw pressure without labeling

### 4. Keep room for GPS-assisted enhancement

Later, when `GPS (NMEA)` is configured and delivering a stable altitude, Air360 may offer:

- “use configured altitude”
- “use GPS altitude”

The first version should not depend on GPS being present.

## Affected Files

- `firmware/main/include/air360/sensors/sensor_types.hpp`
- `firmware/main/include/air360/config_types.hpp`
- `firmware/main/src/config_repository.cpp`
- `firmware/main/src/sensors/sensor_manager.cpp`
- `firmware/main/src/web_ui.cpp`
- `firmware/main/src/status_service.cpp`
- related firmware docs

## Alternatives Considered

### Option A. Raw pressure only

Simple but repeatedly misunderstood by users and ecosystem consumers.

### Option B. Replace raw pressure with normalized pressure everywhere

Convenient but obscures the real sensor output and makes diagnostics harder.

### Option C. Keep raw pressure and add normalized pressure explicitly (accepted)

Provides clear semantics and avoids reinterpreting hardware measurements silently.

## Reference Links

- [Sensor.Community ecosystem review for Air360](../../ecosystem/sensor-community-issues-prs-review-2026-04-14.md)
- [Configuration Reference](../configuration-reference.md)
- [BME280](../sensors/bme280.md)
- [Measurement Pipeline](../measurement-pipeline.md)
- [GPS (NMEA)](../sensors/gps_nmea.md)
- [#102 BME280 pressure shows wrong values](https://github.com/opendata-stuttgart/sensors-software/issues/102)
- [#71 Einheitendiskrepanz Luftdruck zwischen luftdaten und sensemap](https://github.com/opendata-stuttgart/sensors-software/issues/71)
- [#936 Wrong format of 'pressure value' of a BMP280 Sensor](https://github.com/opendata-stuttgart/sensors-software/issues/936)

## Practical Conclusion

Air360 should treat sea-level pressure as a derived, user-relevant value built on top of raw station pressure. That removes a repeated source of confusion without compromising sensor truth.
