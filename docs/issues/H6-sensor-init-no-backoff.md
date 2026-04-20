# H6 — `SensorManager` retries driver init with no backoff

- **Severity:** High
- **Area:** Sensor runtime / bus health
- **Files:**
  - `firmware/main/src/sensors/sensor_manager.cpp`
  - `firmware/main/src/sensors/drivers/*.cpp` (several set `initialized_ = false` on any poll error)

## What is wrong

On any poll error, the driver sets `initialized_ = false`. The next iteration of `SensorManager::taskMain` calls `driver->init()` again. No backoff, no escalation, no cap.

`init()` itself is expensive: it talks to the bus, allocates memory (e.g. `std::make_unique<TinyGPSPlus>()` in the GPS driver, command buffers in SPS30), and — on a shared bus — delays every other sensor's poll.

## Why it matters

- **Bus contention storm.** A single stuck sensor pegs the I²C bus with repeated init probes, starving healthy sensors on the same bus.
- **Heap churn.** Drivers that allocate in `init()` do so once per failed poll. Fragmentation compounds.
- **No visibility.** From the outside the sensor looks "flapping"; the status page shows it alternately initialized and failed, with no indication that the device has tried 10 000 times.

## Consequences on real hardware

- One dead sensor on a shared bus degrades the entire bus.
- Long-term heap drift on devices with a single unreliable sensor.
- Users see inconsistent readings from healthy sensors when one is faulty.

## Fix plan

1. **Per-sensor exponential backoff** in `SensorManager`:
   ```cpp
   struct SensorRuntime {
       uint32_t consecutive_failures = 0;
       uint64_t next_init_allowed_ms = 0;
       // ...
   };

   constexpr uint32_t kInitBackoffBaseMs = 1'000;
   constexpr uint32_t kInitBackoffCapMs  = 5 * 60 * 1'000;
   ```
   On poll or init failure:
   ```cpp
   const uint32_t delay = std::min(
       kInitBackoffBaseMs << std::min<uint32_t>(rt.consecutive_failures, 8),
       kInitBackoffCapMs);
   rt.next_init_allowed_ms = now_ms + delay;
   rt.consecutive_failures++;
   ```
   Before calling `init()` again, compare `now_ms` to `next_init_allowed_ms`.
2. **Reset the counter** on a successful poll.
3. **Escalation to `SensorStatus::kFailed`.** After N consecutive failures across backoff attempts (e.g. 16, covering ~30 min at cap), mark the sensor failed and stop retry attempts. Expose the state on the status page. Require a manual re-enable (via web UI or config reload) to re-attempt.
4. **Drivers: distinguish soft vs hard errors.** A single `poll()` bus glitch should not necessarily force full re-init. Allow N (e.g. 3) consecutive poll failures before tearing down and re-initializing. This is addressed in M5 but shares code with this fix.
5. **Driver allocations move out of `init()`.** Example: `GpsNmeaSensor` should allocate `TinyGPSPlus` once in the constructor or in a dedicated one-shot `warmup()` pass, not in `init()` on every retry.
6. **Backoff log line.** When a sensor enters backoff, log once at WARN with the next retry time. Do not log every retry — that drowns the log.
7. **Surface on status JSON.** `sensor.failures`, `sensor.next_retry_ms`, `sensor.status`.

## Verification

- Disconnect a sensor during runtime; verify the re-init cadence grows geometrically and caps at 5 min.
- Other sensors on the same bus keep polling on schedule.
- After reconnecting the sensor, the manager recovers within one backoff cycle.
- Heap high-water mark over a 24 h soak with one permanently-disconnected sensor stays flat.

## Related

- M5 — soft-vs-hard error distinction is the fix for driver-level `initialized_=false`-on-any-error behavior.
- C1 — a shared-HAL issue can masquerade as sensor init failures; fix C1 first to isolate this one.
