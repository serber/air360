# Live Sensor Reconfiguration ADR

## Status

Implemented.

This document now records an implemented architecture decision for the firmware.

## Prerequisite

This ADR assumes the direction described in [`implemented-measurement-runtime-separation-adr.md`](implemented-measurement-runtime-separation-adr.md):

- `SensorManager` owns driver lifecycle and polling
- the measurement runtime owns latest values and the single bounded queue

Without that separation, live sensor reconfiguration is still possible, but the ownership boundaries remain harder to reason about.

## Decision Summary

Air360 should support applying sensor configuration changes without rebooting the device.

The initial live reconfiguration scope is limited to:

- enabling a sensor
- disabling a sensor
- deleting a sensor
- editing a sensor in place, including:
  - `poll interval`
  - transport-specific binding data such as `i2c address`
  - transport-specific binding data such as `GPIO pin`

The system should keep existing queued measurements for that sensor.

That means:

- disabling or deleting a sensor stops future sampling
- already queued measurements for that sensor remain valid and may still be uploaded
- re-enabling or editing a sensor does not clear its queued measurements
- after an edit, new samples from the updated sensor configuration continue to enter the same global queue

## Context

Today the firmware stages sensor changes in memory, persists them, and then reboots the whole device.

That behavior is operationally simple, but it is heavier than necessary because:

- `SensorManager` can already rebuild runtime sensor state without a full device reboot
- sensor edits such as `poll interval` or `i2c address` do not inherently require rebooting Wi-Fi, backend runtime, or the web server
- rebooting increases friction during setup and sensor experimentation

The design goal is not to support arbitrary runtime mutation with no interruption at all. The design goal is:

- apply sensor changes immediately
- keep the rest of the firmware alive
- preserve queued measurements already collected

## Goals

- apply common sensor changes without device reboot
- keep the web UI, network stack, and backend runtime alive during sensor changes
- preserve already queued measurements
- make enable/disable/edit behavior predictable
- keep implementation simple and robust rather than micro-optimizing for zero downtime

## Non-Goals

- preserving driver-internal state across a sensor edit
- hot-swapping one sensor with zero interruption to the sensor task
- per-sensor queue retention rules
- deleting already queued historical measurements when a sensor is removed
- fully transactional rollback of every possible runtime edge case in the first version

## Architectural Decision

### 1. Live apply should restart the sensor runtime, not the whole device

The preferred implementation model is:

- persist the updated sensor config
- rebuild the sensor runtime
- keep the rest of the firmware running

In practice, that means:

- `SensorManager` may stop and re-create its managed sensors
- the device itself must not reboot as part of normal sensor apply

This is acceptable even if applying sensor changes causes a short polling interruption.

### 2. The sensor queue is not reset by live apply

Queued measurements belong to the measurement runtime, not to the current lifetime of a driver instance.

Therefore:

- applying sensor changes must not clear the global measurement queue
- queued samples already collected before the edit remain uploadable

This is especially important for:

- sensor delete
- sensor disable
- sensor edit that changes transport binding or polling interval

### 3. Sensor identity remains stable across edit operations

For an in-place edit, the logical sensor identity should remain the same.

That means:

- editing `poll interval`
- editing `i2c address`
- editing `GPIO pin`
- toggling `enabled`

should preserve the same `sensor_id`

This lets the queue continue to contain historical samples for the same logical sensor record, even if future samples are collected under updated settings.

### 4. Delete and disable stop future sampling only

For both delete and disable operations:

- future polling stops
- future samples are no longer generated
- already queued samples remain in the queue until uploaded or evicted by the normal bounded-queue policy

The system should not treat existing queued samples as invalid merely because the sensor is no longer active.

### 5. Edit operations produce a continuous queue stream

If a sensor is edited in place:

- existing queued samples remain in the queue
- new samples from the updated configuration are appended later

This means a single sensor queue history may contain:

- samples produced before the edit
- samples produced after the edit

That is acceptable because the queue is an operational upload buffer, not a strict configuration-versioned history store.

## Supported Operation Semantics

### Enable

When a disabled sensor is enabled:

- its record remains the same
- `SensorManager` initializes or rebuilds the corresponding driver
- future samples start being produced again
- old queued samples, if any, remain untouched

### Disable

When an enabled sensor is disabled:

- its record remains in the stored config
- it remains visible in the UI as a configured sensor
- its driver is stopped
- future samples stop
- queued samples already collected remain in the queue

Disable is a temporary state change, not a removal of configuration.

### Edit

When a sensor is edited in place:

- the record keeps the same `sensor_id`
- the updated config is persisted
- the driver is rebuilt using the new settings
- future samples reflect the new settings
- old queued samples remain in the queue

Examples:

- changing `poll interval`
- changing `i2c address`
- changing `GPIO pin`
- changing sensor-specific transport parameters

### Delete

When a sensor is deleted:

- its record is removed from the stored config
- the driver is removed from the active runtime
- it no longer appears as a configured sensor in the UI
- no future samples are produced
- queued samples with that sensor's previous `sensor_id` remain uploadable

Delete is a configuration removal, not a temporary off state.

## Alternatives Considered

### Option A. Keep reboot-based apply

Rejected as the preferred long-term direction.

Why:

- slows setup and experimentation
- restarts unrelated subsystems
- is heavier than needed for common sensor edits

### Option B. Live apply by rebuilding the sensor runtime while preserving the measurement runtime

Accepted direction.

Why:

- simple enough to reason about
- preserves queue continuity
- avoids unnecessary full-device restart
- matches the intended separation between sensor lifecycle and measurement ownership

### Option C. Fine-grained hot patching of individual sensor instances without any manager restart

Rejected for the first version.

Why:

- significantly more complexity
- more chances for partial-update bugs
- little practical value compared with a short sensor-runtime restart

## Expected Data Flow During Apply

Preferred sequence:

1. user submits a sensor change
2. firmware validates the new config
3. firmware saves the new sensor config
4. `SensorManager` rebuilds its active sensor set from that config
5. the measurement runtime remains alive
6. queued samples remain untouched
7. new samples continue to be appended after the updated sensor runtime starts polling again

## UI Implications

The UI should stop framing sensor changes as `Apply and reboot`.

Preferred behavior:

- `Apply now`
- show a short notice that sensor polling will briefly restart
- keep the page available while the new sensor runtime comes up

For delete and disable behavior, the UI should be explicit:

- removing or disabling a sensor stops future sampling
- already queued measurements may still be uploaded

## Status And Diagnostics

After live apply, diagnostics should make it clear:

- whether the sensor runtime was rebuilt successfully
- which sensors are currently active
- the current latest value for each active sensor
- the queued measurement count per sensor, independent of whether that sensor is still active

## Failure Handling

If the new config is valid enough to persist but one sensor fails to initialize at runtime:

- the device should not reboot
- the failing sensor should enter an error state
- other sensors should continue running
- queued historical samples must remain intact

This keeps the failure local to the affected sensor instead of turning it into a system-wide outage.

## Recommended Implementation Direction

The preferred order is:

1. complete the measurement-runtime separation
2. remove sensor apply reboot behavior from the web UI flow
3. save config and call live sensor apply through `SensorManager`
4. keep the queue and latest-measurement runtime alive across apply
5. clarify UI messaging around delete/disable/edit semantics

## Practical Conclusion

Air360 should support live sensor reconfiguration by restarting only the sensor runtime and preserving the measurement runtime.

This gives the desired behavior:

- no device reboot
- queued measurements survive
- deleted or disabled sensors stop producing new data
- edited sensors continue with new settings

It is the simplest architecture that satisfies the required UX without introducing unnecessary queue complexity.
