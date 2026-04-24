# Finding F10: Sensirion HAL uses a global I2C context

## Status

Implemented.

## Scope

Task file for replacing or guarding global mutable I2C context in the SPS30 Sensirion HAL shim.

## Source of truth in code

- `firmware/main/src/sensors/drivers/sensirion_i2c_hal.cpp`
- `firmware/main/src/sensors/drivers/sps30_sensor.cpp`
- `firmware/main/include/air360/sensors/drivers/sps30_i2c_support.hpp`

## Read next

- `docs/firmware/sensors/sps30.md`
- `docs/firmware/transport-binding.md`

**Priority:** Medium
**Category:** Architecture / C++ / Reliability
**Files / symbols:** `sensirion_i2c_hal.cpp`, `Sps30Sensor`, `SensirionI2cContextGuard`

## Problem

Resolved. The Sensirion SPS30 C HAL shim still has to keep the active `i2c_dev_t*` for the vendor C callbacks, but access is now serialized through `SensirionI2cContextGuard`.

## Why it matters

The guard prevents future Sensirion C-driver users or diagnostics from clobbering the active HAL device while another vendor call is in progress. Reads and writes now happen only while the guard owns the static FreeRTOS mutex.

## Evidence

- `firmware/main/src/sensors/drivers/sensirion_i2c_hal.cpp` implements `SensirionI2cContextGuard`.
- The guard lazily creates a static FreeRTOS mutex, takes it before setting `g_device`, clears `g_device` in the destructor, and releases the mutex.
- `sensirion_i2c_hal_read()` and `sensirion_i2c_hal_write()` still reject calls when no guarded context is active.
- `firmware/main/src/sensors/drivers/sps30_sensor.cpp` wraps SPS30 setup, wake, start, and poll vendor calls in `SensirionI2cContextGuard`.

## Recommended Fix

Keep every Sensirion vendor-library call that can perform I2C wrapped in `SensirionI2cContextGuard`. The vendor C API cannot accept per-call context parameters, so the guarded global context is the supported concurrency model.

## Where To Change

- `firmware/main/src/sensors/drivers/sensirion_i2c_hal.cpp`
- `firmware/main/include/air360/sensors/drivers/sps30_i2c_support.hpp`
- `firmware/main/src/sensors/drivers/sps30_sensor.cpp`
- Future Sensirion drivers if added
- `docs/firmware/sensors/sps30.md`
- `docs/firmware/transport-binding.md`

## How To Change

1. `SensirionI2cContextGuard` sets context in the constructor and clears it in the destructor.
2. The remaining global context is protected with a static FreeRTOS mutex.
3. SPS30 wraps every vendor call that may perform I2C.
4. Docs state the supported concurrency model.

## Example Fix

```cpp
class SensirionI2cContextGuard {
  public:
    explicit SensirionI2cContextGuard(i2c_dev_t* dev) {
        lockSensirionHal();
        g_device = dev;
    }
    ~SensirionI2cContextGuard() {
        g_device = nullptr;
        unlockSensirionHal();
    }
};
```

## Validation

- Host test with fake `i2c_dev_read/write` verifies context is set and cleared on early returns.
- Hardware test with SPS30 still polls normally.
- If a second Sensirion driver is later added, run both in the same sensor task and verify no cross-device access.
- Run `source ~/.espressif/v6.0/esp-idf/export.sh && idf.py build` from `firmware/`.
- Run `python3 scripts/check_firmware_docs.py`.

## Risk Of Change

Medium. The SPS30 vendor library is C code with global hooks, so regression testing on hardware is required.

## Dependencies

None.

## Suggested Agent Type

C++ refactoring agent / ESP-IDF agent
