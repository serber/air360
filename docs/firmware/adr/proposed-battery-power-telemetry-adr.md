# Battery And Power Telemetry ADR

## Status

Proposed.

## Decision Summary

Add optional battery and power telemetry for off-grid, UPS-backed, and cellular deployments.

The first version should prefer an external `INA219` over I2C and expose:

- bus or battery voltage
- current draw
- power
- simple charge estimate when configured

## Context

Power visibility is a repeated ecosystem request:

- [#995 Support for battery reporting](https://github.com/opendata-stuttgart/sensors-software/issues/995)
- [PR #1038 Battery monitor feature v2 - based on INA219 Current&Power Monitor](https://github.com/opendata-stuttgart/sensors-software/pull/1038)
- [PR #847 Battery monitor feature](https://github.com/opendata-stuttgart/sensors-software/pull/847)
- [#99 Extension: Solar Panel + Battery](https://github.com/opendata-stuttgart/sensors-software/issues/99)
- [#104 Request: deep sleep for ESP8266 plus battery sensing for solar operation](https://github.com/opendata-stuttgart/sensors-software/issues/104)

Air360 is already more deployment-oriented than legacy ESP8266 firmware:

- ESP32-S3 platform
- cellular uplink option
- existing I2C transport management
- status page and richer runtime model

That makes power telemetry directly useful, not just decorative.

## Goals

- Support battery or UPS-backed installations.
- Make device power state visible in the local UI and status API.
- Keep the first implementation electrically safe and predictable.

## Non-Goals

- Implementing deep-sleep fleet scheduling in this ADR.
- Supporting every analog battery-divider wiring variant in the first version.
- Sending battery data to Sensor.Community.

## Architectural Decision

### 1. Start with INA219 as the preferred path

`INA219` is the preferred first integration because it provides:

- voltage
- current
- power

without forcing the firmware to guess board-specific ADC scaling or divider wiring.

### 2. Treat power telemetry as a separate optional sensor family

Do not hide battery logic inside existing climate or backend code.

Add it as an explicit optional sensor or device capability with:

- dedicated config
- dedicated runtime status
- explicit value kinds if uploaded to rich backends

### 3. Keep charge percentage configuration simple

If a user wants a battery percentage estimate, allow:

- minimum voltage
- maximum voltage

The firmware can then derive a simple bounded percentage.

This is sufficient for field monitoring without pretending to be a full battery management system.

### 4. Limit upload targets initially

Battery and power metrics should be visible locally first.

If uploaded, they should go only to backends that can consume rich internal telemetry such as:

- `Air360 API`
- future `InfluxDB 2`

## Affected Files

- sensor config and sensor registry files under `firmware/main/src/sensors/`
- `firmware/main/include/air360/sensors/sensor_types.hpp`
- `firmware/main/src/status_service.cpp`
- `firmware/main/src/web_ui.cpp`
- uploader mappings for rich backends
- related firmware docs

## Alternatives Considered

### Option A. ADC-divider-only battery reading first

Possible, but board and wiring assumptions make it easier to misconfigure and harder to support.

### Option B. INA219 first (accepted)

Cleaner electrical story and richer telemetry from day one.

### Option C. No power telemetry

Rejected for remote and off-grid deployments where power is a primary operational unknown.

## Reference Links

- [Sensor.Community ecosystem review for Air360](../../ecosystem/sensor-community-issues-prs-review-2026-04-14.md)
- [Configuration Reference](../configuration-reference.md)
- [Implemented cellular uplink ADR](implemented-cellular-uplink-adr.md)
- [#995 Support for battery reporting](https://github.com/opendata-stuttgart/sensors-software/issues/995)
- [PR #1038 Battery monitor feature v2 - based on INA219 Current&Power Monitor](https://github.com/opendata-stuttgart/sensors-software/pull/1038)
- [PR #847 Battery monitor feature](https://github.com/opendata-stuttgart/sensors-software/pull/847)
- [#99 Extension: Solar Panel + Battery](https://github.com/opendata-stuttgart/sensors-software/issues/99)
- [#104 Request: deep sleep for ESP8266 plus battery sensing for solar operation](https://github.com/opendata-stuttgart/sensors-software/issues/104)

## Practical Conclusion

Air360 should add power telemetry in a way that is operationally useful and easy to wire correctly. `INA219` is the right first step because it fits the current hardware and firmware architecture cleanly.
