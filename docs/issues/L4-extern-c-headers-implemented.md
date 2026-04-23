# L4 — C++ headers inside `extern "C"` blocks

- **Severity:** Low
- **Area:** Code hygiene
- **Files:**
  - `firmware/main/src/sensors/drivers/bme280_sensor.cpp` (`extern "C" { #include "bme280.h" }`)
  - Audit other driver files for similar patterns.

## What is wrong

The pattern:

```cpp
extern "C" {
#include "bme280.h"
}
```

is correct when `bme280.h` is a C header and includes only C declarations. It becomes a landmine when:

- The included header transitively includes a C++ header (or a header that, on a different ESP-IDF version, starts including one).
- Another `extern "C"` wrapper inside the chain creates nested language linkage blocks (harmless but confusing).

## Why it matters

- Future ESP-IDF or vendor-library updates can start transitively including C++ headers, leading to obscure compile errors.
- Makes the code look defensive in ways that may or may not be needed — ESP-IDF C headers typically guard themselves with `#ifdef __cplusplus extern "C" {}`.

## Consequences on real hardware

- None today.

## Fix plan

1. **Check each C header** for an internal `extern "C"` guard. Most ESP-IDF / Sensirion / BME280 headers have one.
2. **If the header self-guards,** drop the outer `extern "C" {}` wrapper — it is redundant.
3. **If the header does not self-guard,** file a patch upstream or add the guard locally in an `air360/<vendor>_wrapper.h` shim that this TU includes instead.
4. **Document in `AGENTS.md`:** prefer self-guarded C headers; only wrap when necessary.

## Verification

- Build clean after the removal.
- `grep -rn "extern \"C\"" firmware/main/src/` shows only the minimum necessary.

## Related

- None.

## Implemented

Implemented.

- Removed redundant outer `extern "C"` include wrappers from:
  - `firmware/main/src/sensors/drivers/bme280_sensor.cpp`
  - `firmware/main/src/sensors/drivers/bme680_sensor.cpp`
  - `firmware/main/src/sensors/drivers/sps30_sensor.cpp`
- Verified that the included vendor headers self-guard with `#ifdef __cplusplus extern "C"`.
- Added a project rule to `AGENTS.md`: prefer self-guarded C headers and only introduce wrappers/shims when a header does not guard itself.
- `rg -n "extern \"C\"" firmware/main/src` now shows only the minimum necessary call sites.
