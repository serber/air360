# Targeted Sensor Expansion ADR

## Status

Proposed.

## Decision Summary

Expand sensor support only where the new device fits the current Air360 measurement model and closes repeated ecosystem demand.

The recommended order is:

1. `Honeywell IH-PMC-001`
2. `Senseair S8` or `MH-Z14`
3. `CCS811`

## Context

The Sensor.Community tracker includes many sensor requests, but not all are equally valuable for Air360.

The most actionable current candidates are:

- [#1059 Feature request: add support for Honeywell IH-PMC-001](https://github.com/opendata-stuttgart/sensors-software/issues/1059)
- [PR #992 Support for CCS811 sensor](https://github.com/opendata-stuttgart/sensors-software/pull/992)
- [#878 Add support for CO2 sensor Senseair S8](https://github.com/opendata-stuttgart/sensors-software/issues/878)
- [#949 Add support for MH-Z14 CO2 Module](https://github.com/opendata-stuttgart/sensors-software/issues/949)

Air360 already has a reasonably clean measurement model with existing value kinds for:

- particulate matter families
- `CO2 ppm`
- climate values

That means some new sensors fit naturally, while others require new semantics and uploader decisions.

## Goals

- Add new sensors only where they map cleanly to existing architecture.
- Prioritize sensors that close repeated user demand without destabilizing the system.
- Avoid sensor sprawl for devices that need entirely new semantics or backend contracts.

## Non-Goals

- Mirroring the full historical airRohr sensor wishlist.
- Adding every “interesting” sensor with no clear upload or UI story.
- Making sensor count the primary measure of firmware progress.

## Architectural Decision

### 1. PM sensor expansions should come first if they map to existing PM value kinds

`Honeywell IH-PMC-001` is a strong candidate because:

- it answers a current upstream request
- it is still in the core PM domain
- the existing value kinds for PM and particle counts may already be sufficient

### 2. Additional CO2 sensors are next because `CO2 ppm` already exists

`Senseair S8` and `MH-Z14` fit the current measurement model better than many exotic gas sensors because Air360 already has:

- `CO2 ppm`
- climate and environmental context
- uploader support for `CO2`

### 3. CCS811 should wait until value semantics are agreed

`CCS811` is appealing, but it raises design questions:

- which value kinds should be first-class?
- how should `TVOC` and `eCO2` be represented?
- which backends should receive them?

That makes it a lower-priority expansion than PM or direct-CO2 additions.

### 4. Every new sensor should pass the same admission test

Before adding a sensor, confirm:

- transport fit
- measurement kind fit
- UI presentation fit
- backend mapping fit
- diagnostics and configuration fit

If those are unclear, the sensor is not yet ready for implementation.

## Affected Files

- `firmware/main/include/air360/sensors/sensor_types.hpp`
- `firmware/main/src/sensors/sensor_registry.cpp`
- new driver files under `firmware/main/src/sensors/drivers/`
- uploader mappings where needed
- related firmware docs

## Alternatives Considered

### Option A. Add sensors opportunistically whenever a request appears

Leads to sensor sprawl and uneven quality.

### Option B. Expand only where the measurement model already has a good fit (accepted)

Keeps new support useful and maintainable.

### Option C. Freeze all new sensor work until all reliability work is done

Too rigid. Some new sensors can still be sensible in parallel when they are low-risk and architecturally clean.

## Reference Links

- [Sensor.Community ecosystem review for Air360](../../ecosystem/sensor-community-issues-prs-review-2026-04-14.md)
- [Supported sensors](../sensors/supported-sensors.md)
- [Sensor registry](../PROJECT_STRUCTURE.md)
- [#1059 Feature request: add support for Honeywell IH-PMC-001](https://github.com/opendata-stuttgart/sensors-software/issues/1059)
- [PR #992 Support for CCS811 sensor](https://github.com/opendata-stuttgart/sensors-software/pull/992)
- [#878 Add support for CO2 sensor Senseair S8](https://github.com/opendata-stuttgart/sensors-software/issues/878)
- [#949 Add support for MH-Z14 CO2 Module](https://github.com/opendata-stuttgart/sensors-software/issues/949)

## Practical Conclusion

Air360 should add sensors selectively, not reactively. The right next additions are the ones that reuse the current measurement model cleanly and solve a visible ecosystem request without forcing architectural debt.
