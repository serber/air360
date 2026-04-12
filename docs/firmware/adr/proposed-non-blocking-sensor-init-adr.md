# Non-Blocking Sensor Init ADR

## Status

Proposed.

## Decision Summary

Enforce a non-blocking contract on `ISensorDriver::init()` and add a `kWarmupRequired` result to handle mandatory post-init delays through the existing scheduling mechanism instead of `vTaskDelay()`.

## Context

The `air360_sensor` task initializes and polls all sensors sequentially in a single loop iteration. Each sensor's `init()` is called when `driver_ready == false`. If an `init()` implementation calls `vTaskDelay()` internally — for example, to wait for a sensor reset or a mandatory warm-up period — it blocks the entire task for that duration. All other sensors are delayed for the same period.

Real examples where a driver might need a post-init delay:

| Sensor | Reason | Typical delay |
|--------|--------|---------------|
| SCD30 | Requires stabilization after measurement start command | 2 000 ms |
| BME680 | Requires a heat-up cycle before gas resistance is valid | 150–300 ms |
| GPS (NMEA) | First valid NMEA sentence takes seconds after power-on | 1 000–5 000 ms |
| SPS30 | Fan spin-up before first measurement | 1 000 ms |

With 3–4 sensors each blocking for 1–2 seconds, the first full measurement cycle is delayed by the sum of all warm-up times rather than the maximum.

The startup pipeline itself is not affected — the sensor task runs independently from the main task. The problem is **time-to-first-measurement** and violation of the project coding rule: *no blocking delays > 50 ms in shared tasks*.

## Goals

- Enforce that `init()` never blocks longer than a single I2C transfer (~5 ms at 100 kHz) or a single UART exchange.
- Allow drivers that need a post-init warm-up to express that delay without blocking.
- Handle warm-up delays through the existing `next_action_time_ms` scheduling mechanism.
- Not require additional FreeRTOS tasks or synchronization primitives.

## Non-Goals

- True parallel initialization across sensors (not needed while all I2C sensors share a single bus protected by a mutex).
- Changing the poll scheduling logic beyond warm-up handling.
- Retroactively auditing every driver for internal delays in the first pass — establish the contract and fix drivers as they are encountered.

## Architectural Decision

### 1. Add `kWarmupRequired` to the driver init result

Extend the init return type (currently `esp_err_t`) to communicate a warm-up intent. The cleanest approach without changing the return type is to add a `warmup_ms` output parameter:

```cpp
// ISensorDriver interface — updated init signature
virtual esp_err_t init(SensorDriverRecord& record,
                       SensorDriverContext& context,
                       uint32_t& warmup_ms_out) = 0;
```

- If `warmup_ms_out == 0`: driver is ready to poll immediately (current behavior).
- If `warmup_ms_out > 0`: hardware is configured and responding, but first poll must not happen before `warmup_ms_out` milliseconds.

### 2. Update `SensorManager::taskMain()` to respect warm-up

After a successful `init()` call, if `warmup_ms_out > 0`, set:

```cpp
managed_sensor.driver_ready = true;
managed_sensor.state = SensorState::kInitialized;
managed_sensor.next_action_time_ms = uptimeMilliseconds() + warmup_ms_out;
```

This defers the first `poll()` call by the declared warm-up duration. No blocking, no extra tasks — the scheduler naturally skips this sensor until the warm-up window has passed.

### 3. Driver contract (documented in `ISensorDriver`)

Add to the `ISensorDriver` interface documentation:

> `init()` must complete within the time of a single bus transaction (≤ 50 ms). If the sensor requires a stabilization period before the first valid measurement, set `warmup_ms_out` to the required delay and return `ESP_OK`. The sensor manager will schedule the first `poll()` call after this delay. `init()` must not call `vTaskDelay()` or any other blocking primitive.

### 4. Drivers to update

Audit all existing drivers and remove any `vTaskDelay()` from `init()`, replacing with `warmup_ms_out`:

| Driver | Change |
|--------|--------|
| `scd30_driver.cpp` | Remove reset delay; set `warmup_ms_out = 2000` |
| `bme680_driver.cpp` | Remove heat-up wait if present; set `warmup_ms_out = 300` |
| `sps30_driver.cpp` | Remove fan spin-up wait if present; set `warmup_ms_out = 1000` |
| `gps_nmea_driver.cpp` | Remove UART settle delay if present; set `warmup_ms_out = 2000` |

Drivers with no mandatory delay set `warmup_ms_out = 0` and require no change beyond the updated signature.

## Affected Files

- `firmware/main/include/air360/sensors/sensor_driver.hpp` — update `ISensorDriver::init()` signature, add warm-up contract to docstring
- `firmware/main/src/sensors/sensor_manager.cpp` — update `taskMain()` to read `warmup_ms_out` and set `next_action_time_ms` accordingly
- `firmware/main/src/sensors/drivers/*.cpp` — remove `vTaskDelay()` from `init()` implementations, set `warmup_ms_out` where needed

## Alternatives Considered

### Option A. Per-sensor initialization tasks

Spawn a temporary FreeRTOS task per sensor at init time. Each task runs `init()` + `vTaskDelay(warmup)` then signals via event group. The polling loop starts after all signals received.

Rejected: adds stack overhead per sensor, adds synchronization complexity, and provides no real parallelism for I2C sensors that share a mutex-protected bus.

### Option B. Keep `vTaskDelay()` in drivers, document the limit

Accept blocking delays up to some cap (e.g., 500 ms). Simple but violates the coding rule and compounds linearly with sensor count.

### Option C. `warmup_ms_out` output parameter (accepted)

Zero additional tasks, zero additional synchronization. Reuses the existing `next_action_time_ms` scheduling that already handles poll retries. Clean interface contract enforceable at review time.

## Practical Conclusion

Add `warmup_ms_out` to `ISensorDriver::init()`. In `SensorManager::taskMain()`, apply the warm-up delay via `next_action_time_ms` on successful init. Audit existing drivers and replace any `vTaskDelay()` in `init()` with the appropriate `warmup_ms_out` value. The result: all sensors initialize without blocking each other, warm-up periods are respected, and time-to-first-measurement equals `max(warmup_ms)` across all sensors rather than `sum(warmup_ms)`.
