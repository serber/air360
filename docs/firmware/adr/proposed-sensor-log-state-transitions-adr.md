# Sensor Log on State Transitions ADR

## Status

Proposed.

## Decision Summary

Change `SensorManager` to log sensor errors and recoveries only on state transitions, not on every poll iteration.

## Context

The sensor task polls every 250 ms. When a sensor enters an error state (hardware absent, I2C timeout, driver failure), the error is logged at `ESP_LOGW` or `ESP_LOGE` on every poll attempt:

```
W (air360.sensor) BME280[1]: poll failed: ESP_ERR_TIMEOUT
W (air360.sensor) BME280[1]: poll failed: ESP_ERR_TIMEOUT
W (air360.sensor) BME280[1]: poll failed: ESP_ERR_TIMEOUT
... (every 250 ms until sensor recovers or device reboots)
```

A sensor absent for one hour produces ~14 400 identical log lines. On a live serial monitor this makes distinguishing real events from noise extremely difficult. Log spam also fills the UART buffer faster than it can drain, causing log interleaving with other tasks.

## Goals

- Produce exactly one log message when a sensor transitions into an error state.
- Produce exactly one log message when a sensor recovers.
- Keep full error detail (error code, driver message) in the transition log.
- Preserve debug-level logging for every poll if needed during development.

## Non-Goals

- Changing the sensor state machine or retry logic.
- Suppressing all error information — the transition log must still carry the reason.

## Architectural Decision

Add `SensorState previous_state` to `ManagedSensor`. After each `init()` or `poll()` call in `taskMain()`, compare the new state to `previous_state`:

```cpp
const SensorState new_state = computeNewState(...);
if (new_state != managed_sensor.previous_state) {
    if (isErrorState(new_state)) {
        ESP_LOGW(kTag, "Sensor %s [%u]: entered %s — %s",
                 name, id, stateName(new_state), driver->lastError());
    } else if (isOperationalState(new_state) && isErrorState(managed_sensor.previous_state)) {
        ESP_LOGI(kTag, "Sensor %s [%u]: recovered → %s",
                 name, id, stateName(new_state));
    }
    managed_sensor.previous_state = new_state;
}
```

Verbose per-poll logging moves to `ESP_LOGD` (debug level, compiled out in release builds by default).

## Affected Files

- `firmware/main/src/sensors/sensor_manager.cpp` — add `previous_state` field to `ManagedSensor`, change logging logic in `taskMain()`
- `firmware/main/include/air360/sensors/sensor_manager.hpp` — update `ManagedSensor` struct if defined there

## Alternatives Considered

### Option A. Keep per-poll logging

Simple, but produces massive log noise in failure scenarios. Makes production monitoring unusable.

### Option B. Rate-limit with a timer (log at most once per N seconds)

Avoids duplicate per-poll logs but requires a per-sensor timestamp and still produces repeated messages on long outages.

### Option C. Log on state transitions only (accepted)

Zero repeated messages. Full context at the moment of transition. Clean recovery signal. No timer needed.

## Practical Conclusion

Track `previous_state` in `ManagedSensor`. Log once on entry into error state, once on recovery. Move per-poll detail to `ESP_LOGD`. The change is confined to `sensor_manager.cpp`.
