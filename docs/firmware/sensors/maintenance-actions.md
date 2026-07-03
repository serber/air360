# One-shot maintenance actions

## Status

Implemented. Keep this document aligned with the maintenance-action mechanism in the sensor manager, registry, and drivers.

## Scope

This document covers the shared mechanism for **one-shot, run-once** sensor maintenance actions: how they are declared, persisted, scheduled from the web UI, executed by a driver, and cleared after completion. Per-action specifics live in the per-sensor docs (SCD30 FRC, SPS30 fan cleaning).

## Source of truth in code

- `firmware/main/include/air360/sensors/sensor_types.hpp` — `MaintenanceActionKind`
- `firmware/main/include/air360/sensors/sensor_registry.hpp` — `MaintenanceActionDescriptor`, descriptor fields, lookup helpers
- `firmware/main/include/air360/sensors/sensor_driver.hpp` — `MaintenanceActionState` and driver hooks
- `firmware/main/src/sensors/sensor_manager.cpp` — completion detection and clearing
- `firmware/main/src/data/data_layer.cpp` — NVS persistence handler
- `firmware/main/src/sensors/drivers/scd30_sensor.cpp`, `sps30_sensor.cpp` — example actions

## Read next

- [README.md](README.md)
- [supported-sensors.md](supported-sensors.md)
- [scd30.md](scd30.md)
- [sps30.md](sps30.md)
- [../nvs.md](../nvs.md)

## Why this exists, and how it differs from `startup_calibration`

`startup_calibration` is a **persistent mode** — a driver re-asserts it on every `init()` (SCD30 ASC on/off). A maintenance action is the opposite: an operation an operator schedules to run **exactly once** after the next boot (e.g. a forced recalibration or a fan cleaning), after which it must not repeat. The two are independent and can be combined — for example, schedule an SCD30 FRC for the next boot while leaving ASC enabled.

Some actions take **minutes** (SCD30 FRC needs ≥ 2 minutes of stable warm-up). Because all sensors share one watchdog-subscribed manager task, an action must **never block** `init()` or `poll()`. Each action is therefore a non-blocking state machine that advances one step per poll while normal measurements continue.

## Data model

- `SensorRecord::pending_maintenance_action` (`uint8_t`, NVS) holds a `MaintenanceActionKind` value; `0` = none. It is carved from former `reserved1` padding, so no schema bump — see [../nvs.md](../nvs.md).
- A `SensorDescriptor` advertises supported actions via `maintenance_actions` (pointer to a static `MaintenanceActionDescriptor[]`) and `maintenance_action_count`. The web UI only offers actions a sensor advertises, and `SensorRegistry::validateRecord()` rejects a `pending_maintenance_action` the descriptor does not advertise.

## Driver contract

A driver that advertises actions implements three `SensorDriver` hooks (default no-ops otherwise):

- `maintenanceActionState()` → `kIdle` / `kRunning` / `kCompleted` / `kFailed`
- `maintenanceStatus()` → short progress string for the web UI
- `acknowledgeMaintenanceAction()` → return to `kIdle` after the manager records the result

In `init()` the driver reads `record.pending_maintenance_action` and arms its state machine (`kRunning`) when the armed kind matches. In `poll()` it advances the machine while a fresh sample is available, transitioning to `kCompleted` or `kFailed`. Terminal states are **non-fatal**: the sensor keeps measuring.

## Lifecycle

1. **Schedule** — operator picks an action in the per-sensor "Run on next boot" selector; `web_mutating_routes.cpp` writes `pending_maintenance_action`. On apply/reboot it is persisted to NVS.
2. **Arm** — on `init()` the driver arms the matching state machine.
3. **Run** — the manager's poll loop drives `poll()`; the driver advances its state machine and exposes progress through `maintenanceStatus()`, surfaced on the sensor card. While the driver reports `kRunning`, the manager polls that sensor at least every 5 s (`kMaintenanceActivePollIntervalMs`) regardless of its configured poll interval, so a time-based action still makes progress on a slow-polling sensor.
4. **Complete** — when the driver reports `kCompleted`/`kFailed`, `SensorManager` (under its lock) clears the in-memory `pending_maintenance_action`, calls `acknowledgeMaintenanceAction()`, and — after releasing the lock — invokes the handler wired in `DataLayer::bootSensors()` to clear the byte in the persisted config and re-`save()` it to NVS.
5. **Done** — the selector returns to "None"; the action does not run again.

**At-least-once semantics:** a power loss after arming but before the manager clears the byte re-runs the action on the next boot. Actions must be safe to repeat (FRC and fan cleaning both are). Each driver bounds its own retries (FRC has a 300 s timeout → `kFailed`) so a sensor that can never complete does not wedge forever.

## Adding an action to a sensor

1. Add a `MaintenanceActionKind` value in `sensor_types.hpp` and its `maintenanceActionKey()` token.
2. Add a static `MaintenanceActionDescriptor[]` in `sensor_registry.cpp` and wire it into the sensor's descriptor (`maintenance_actions` / `maintenance_action_count`); update the `sizeof(SensorDescriptor)` `static_assert` if the layout changes.
3. Implement the state machine and the three driver hooks; arm it in `init()`, advance it in `poll()`.
4. Update [supported-sensors.md](supported-sensors.md), the per-sensor doc, and [../nvs.md](../nvs.md) if the model changes.
