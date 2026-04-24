# Finding F10: Sensirion HAL uses a global I2C context

## Status

Confirmed audit finding. Not implemented.

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
**Files / symbols:** `sensirion_i2c_hal.cpp`, `Sps30Sensor`, `sps30HalSetContext`, `sps30HalClearContext`

## Problem

The Sensirion SPS30 C HAL shim stores the active `i2c_dev_t*` in a namespace-global pointer. The vendor HAL read/write functions use that pointer without synchronization.

## Why it matters

The current firmware has one SPS30 driver path, so this is not an immediate two-driver collision. It is still a fragile hardware abstraction: adding another Sensirion C-driver sensor or future concurrent diagnostics would make the active I2C device implicit global state. That can cause reads or writes to hit the wrong device.

## Evidence

- `firmware/main/src/sensors/drivers/sensirion_i2c_hal.cpp:15` defines `i2c_dev_t* g_device = nullptr`.
- `firmware/main/src/sensors/drivers/sensirion_i2c_hal.cpp:21` and `25` set and clear that global pointer.
- `firmware/main/src/sensors/drivers/sensirion_i2c_hal.cpp:50` and `60` call `i2c_dev_read(g_device, ...)` and `i2c_dev_write(g_device, ...)`.
- `firmware/main/src/sensors/drivers/sps30_sensor.cpp` calls `sps30HalSetContext(&device_)` before SPS30 operations.

## Recommended Fix

Make the HAL context explicit or task-local. If the vendor C API cannot accept context parameters, wrap context setting in a RAII scope and guard it with a mutex so no other Sensirion driver can clobber it.

## Where To Change

- `firmware/main/src/sensors/drivers/sensirion_i2c_hal.cpp`
- `firmware/main/include/air360/sensors/drivers/sps30_i2c_support.hpp`
- `firmware/main/src/sensors/drivers/sps30_sensor.cpp`
- Future Sensirion drivers if added
- `docs/firmware/sensors/sps30.md`
- `docs/firmware/transport-binding.md`

## How To Change

1. Add a small `SensirionI2cContextGuard` that sets context in the constructor and clears it in the destructor.
2. Protect the global context with a static mutex if it remains global.
3. Use the guard around every vendor SPS30 call that may perform I2C.
4. Prefer `thread_local` or per-task storage if ESP-IDF/toolchain support is acceptable and tested.
5. Add comments stating the supported concurrency model.

## Example Fix

```cpp
class SensirionI2cContextGuard {
  public:
    explicit SensirionI2cContextGuard(i2c_dev_t* dev) {
        lockSensirionHal();
        sps30HalSetContext(dev);
    }
    ~SensirionI2cContextGuard() {
        sps30HalClearContext();
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
