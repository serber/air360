# M5 — DHT/BME280 drivers force full re-init on any error

- **Severity:** Medium
- **Area:** Sensor driver resilience / bus health
- **Files:**
  - `firmware/main/src/sensors/drivers/dht_sensor.cpp`
  - `firmware/main/src/sensors/drivers/bme280_sensor.cpp`
  - Similar pattern likely in other drivers — audit all of `sensors/drivers/*.cpp`

## What is wrong

On any `poll()` error these drivers set `initialized_ = false` and `last_error_ = "..."`. The manager then runs a full re-init on the next cycle.

A single transient bus glitch — an SDA/SCL race, a mutex contention timeout, a cosmic-ray bit — causes a full teardown and rebuild, which on a shared I²C bus serializes behind every other sensor's teardown.

## Why it matters

- Bus-wide degradation from a single transient.
- Heap/stack churn for every glitch.
- Exacerbates the bus-contention pattern when multiple sensors share a bus.

## Consequences on real hardware

- In high-EMI environments (near motors, chargers), transient bus errors occur; devices regress to a low-effective-poll-rate state even though sensors are fine.

## Fix plan

1. **Distinguish soft and hard errors.**
   - Soft: CRC failure, single-read timeout, single NACK. Increment a soft-fail counter. Retain `initialized_ = true`.
   - Hard: repeated soft fails (N=3) within a window, chip-ID mismatch, bus enumeration failure. These set `initialized_ = false`.
2. **Expose the retry state** on the status endpoint.
3. **Reset the soft-fail counter** on any successful poll.
4. **Log WARN on first soft fail, ERROR only on escalation to hard.**
5. **Share the pattern** via a helper in the `SensorDriver` base class (if one exists) or a small mixin:
   ```cpp
   template <typename Derived>
   struct SoftFailPolicy {
       uint32_t soft_fails = 0;
       static constexpr uint32_t kSoftToHardThreshold = 3;
       void onPollOk() { soft_fails = 0; }
       bool onPollErr() {
           return ++soft_fails >= kSoftToHardThreshold;
       }
   };
   ```
6. **Audit every driver** for this pattern and apply uniformly.

## Verification

- Inject a single I²C NACK into a driver; assert `initialized_` stays true and the next poll succeeds normally.
- Inject three NACKs in a row; assert escalation to `initialized_ = false` with one ERROR line.

## Related

- H6 — backoff lives at the manager level; this issue lives at the driver level. Fix both.
- C1 — fix first, otherwise this issue masks HAL problems.
