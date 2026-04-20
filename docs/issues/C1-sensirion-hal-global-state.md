# C1 — Global mutable I²C context in the Sensirion HAL shim

- **Severity:** Critical
- **Area:** Hardware abstraction / concurrency
- **Files:**
  - `firmware/main/src/sensors/drivers/sensirion_i2c_hal.cpp`
  - `firmware/main/src/sensors/drivers/sps30_sensor.cpp`
  - `firmware/main/include/air360/sensors/drivers/sps30_i2c_support.hpp`

## What is wrong

`sensirion_i2c_hal.cpp` holds the current I²C device as a namespace-level pointer:

```cpp
namespace {
i2c_dev_t* g_device = nullptr;
}
```

Callers are required to invoke `sps30HalSetContext(&dev)` before every operation. The Sensirion HAL `read` / `write` callbacks consume this global with no synchronization.

## Why it matters

- Any second Sensirion-based driver (SCD4x, SFA30, SEN5x) sharing the same HAL will clobber the first driver's context on the same bus.
- A single Sensirion driver is also unsafe: if `SensorManager::taskMain` preempts between `sps30HalSetContext()` and the actual HAL read/write (which it can — there is no lock held around the pair), the context can be overwritten by a parallel diagnostics or reconfiguration path.
- The low-level `i2c_dev_read/write` calls will happily talk to the wrong device; errors surface only as checksum failures or nonsensical readings.

## Consequences on real hardware

- Cross-device reads silently return wrong data.
- Adding a second Sensirion sensor breaks PM readings without any log signature pointing at the shared HAL.
- Thread-safety bugs are non-deterministic and bench-testing will not reveal them.

## Fix plan

1. **Decide on context plumbing.** Two acceptable approaches:
   - Pass `i2c_dev_t*` explicitly through every HAL call — preferred when the vendor driver API exposes a context parameter.
   - If the vendor HAL cannot be modified, use `thread_local` storage or a per-task registry keyed by `xTaskGetCurrentTaskHandle()`.
2. **Refactor `sensirion_i2c_hal.cpp`:**
   - Remove the namespace-level `g_device`.
   - If going the thread-local route:
     ```cpp
     static thread_local i2c_dev_t* t_device = nullptr;
     void sps30HalSetContext(i2c_dev_t* d) { t_device = d; }
     void sps30HalClearContext() { t_device = nullptr; }
     // read/write use t_device
     ```
3. **Audit all call sites** (`sps30_sensor.cpp` — every I²C op) so set/clear are exception- and early-return-safe. Prefer a RAII scope guard:
   ```cpp
   struct SensirionScope {
       SensirionScope(i2c_dev_t* d) { sps30HalSetContext(d); }
       ~SensirionScope() { sps30HalClearContext(); }
   };
   ```
4. **Add a debug-build assertion** that the HAL read/write is not called with a null context (currently silently returns `I2C_BUS_ERROR`).
5. **Unit-test** on host: fake `i2c_dev_*` calls, run two simulated drivers concurrently, assert each driver sees its own context.

## Verification

- After the fix, running two Sensirion drivers on the same bus in a loop on hardware yields zero cross-device checksum failures over a 30-minute soak.
- Static analysis: grep confirms no remaining namespace-level `i2c_dev_t*` or similar globals in `sensors/drivers/`.

## Related

- C2 (queue persistence) is independent but often exposed by the same fault path (reading wrong sensor → queued garbage → never re-verifiable once lost).
