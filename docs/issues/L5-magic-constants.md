# L5 — Magic constants lack rationale

- **Severity:** Low
- **Area:** Maintainability
- **Files:**
  - `firmware/main/src/network_manager.cpp` (`kReconnectBaseDelayMs = 10000`, max 300s, etc.)
  - `firmware/main/src/sensors/drivers/gps_nmea_sensor.cpp` (`kGpsReadTimeoutTicks = 100 ms`, `kGpsMaxBytesPerPoll = 2048`)
  - `firmware/main/src/uploads/measurement_store.cpp` (`kMaxQueuedSamples = 256`)
  - `firmware/main/src/ble_advertiser.cpp` (`kUpdateIntervalMs = 5000`)
  - Audit all `constexpr` / `kFoo = N` constants across `main/src/`.

## What is wrong

Many tuning constants sit as bare numeric literals in code with no comment explaining:

- Why this value and not another?
- What bounds the choice (hardware limit, spec requirement, product decision)?
- What other values downstream depend on it?

Examples:
- `kReconnectBaseDelayMs = 10'000` — why 10 s specifically?
- `kMaxQueuedSamples = 256` — sized for what outage / sensor count?
- `kGpsMaxBytesPerPoll = 2048` — matches what UART buffer / baud rate?
- `kUpdateIntervalMs = 5'000` (BLE) — why 5 s?

## Why it matters

- Future maintainers hesitate to change the value, or change it without understanding the consequences.
- No single source of truth for tuning — every constant is its own orphan.

## Consequences on real hardware

- Over time, the constants drift, or one gets changed while a dependent one does not.

## Fix plan

1. **For each tuning constant, add a one-line rationale comment:**
   ```cpp
   // 10 s first retry — short enough to recover a transient drop,
   // long enough to avoid hammering a rebooting AP.
   constexpr std::uint32_t kReconnectBaseDelayMs = 10'000U;
   ```
2. **Promote product-visible constants to Kconfig.** If a field-tunable value exists (queue depth, reconnect timing, poll intervals), make it `CONFIG_AIR360_*` so it can be adjusted per build without a code change.
3. **Cross-reference dependent constants.** Example: `kGpsMaxBytesPerPoll` should reference `CONFIG_AIR360_GPS_UART_BAUD` and `kGpsPollIntervalMs`.
4. **Centralize where possible.** A `tuning.hpp` header with grouped constants can replace scattered definitions for related subsystems.
5. **Document in `configuration-reference.md`** every Kconfig value with its default, purpose, and safe range.

## Verification

- Every `constexpr kFoo = N` in `main/src/` either has a rationale comment, a Kconfig promotion, or both.
- `configuration-reference.md` lists each tunable.

## Related

- H6, H8, M9 — tuning constants for backoff and poll budgets are the first candidates.
