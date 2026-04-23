# M3 — Transport binding hardcodes bus 0 and UART 1/2

- **Severity:** Medium
- **Area:** Hardware abstraction / board-revision resilience
- **Files:**
  - `firmware/main/src/sensors/transport_binding.cpp` (`I2cBusManager`, `UartPortManager`)
  - `firmware/main/src/sensors/sensor_registry.cpp` (validators rejecting bus != 0 with messages about "current board wiring")

## What is wrong

- `I2cBusManager` supports only bus 0. Pins are hardcoded to `CONFIG_AIR360_I2C0_SDA/SCL_GPIO`.
- `UartPortManager` has a `std::array`-backed `ports_` with only ports 1 and 2 accepted. UART 0 is excluded (reasonable — it is console), but no room for future layouts.
- Validators reject descriptors with bus != 0 and return error strings that explicitly mention "current board wiring."

## Why it matters

- The moment a new board revision uses a second I²C bus or a different UART, the abstraction breaks and needs a targeted patch. The abstraction isn't really an abstraction — it's a shim.
- ESP-IDF's `i2c_master` supports up to 2 I²C buses on the S3; not exploiting that is a needless limitation.

## Consequences on real hardware

- Board revision work starts with an internal refactor before any sensor work — demotivating and error-prone.
- Sensors that would benefit from a dedicated bus (high-speed SPS30 vs slow BME280) must share.

## Fix plan

1. **Make bus configuration data-driven.**
   - Introduce a `BusConfig` struct in Kconfig (or a board-profile header) enumerating each I²C bus with SDA/SCL/speed/pullups.
   - `I2cBusManager` accepts a `std::span<const BusConfig>` at init.
2. **Uncap UART ports.** Accept `uart_port_t` values up to `UART_NUM_MAX - 1`, reject UART 0 with a clear error (console reserved), otherwise allow.
3. **Remove the "current board wiring" wording** from validators; replace with "bus id N is not configured on this build."
4. **Add a board-profile abstraction.** `boards/air360_v1.hpp`, `boards/air360_v2.hpp`; selected by Kconfig.
5. **Sensor descriptors reference bus IDs symbolically.** Instead of numeric 0, use `kPrimaryBus` / `kSecondaryBus` enums that the board profile maps to physical IDs.

## Verification

- Build with a synthetic two-bus board profile; sensors on bus 1 initialize correctly.
- Existing board still builds identically; behavior unchanged.

## Related

- M2 — designated initializers make it easier to add `bus_id` changes safely.

## Resolved

- **Date:** 2026-04-23
- **Files changed:**
  - `firmware/main/include/air360/sensors/bus_config.hpp` (new) — `BusConfig` struct
  - `firmware/main/include/air360/boards/air360_v1.hpp` (new) — single-bus board profile
  - `firmware/main/include/air360/boards/air360_v2.hpp` (new) — dual-bus board profile
  - `firmware/main/include/air360/boards/board_profile.hpp` (new) — Kconfig-based profile selector
  - `firmware/main/include/air360/sensors/transport_binding.hpp` — `I2cBusManager::init()` now takes `std::span<const BusConfig>`; `buses_` member added; `UartPortManager::ports_` changed from `array<PortState,2>` to `array<PortState,UART_NUM_MAX>`; `resolvePort()` removed
  - `firmware/main/src/sensors/transport_binding.cpp` — data-driven `resolvePins()` iterates stored span; UART bounds changed from hard cap at 2 to `[1, UART_NUM_MAX)`; all `ports_[port_id - 1]` → `ports_[port_id]`
  - `firmware/main/src/sensors/sensor_manager.cpp` — `init()` call passes `board::kI2cBuses`
  - `firmware/main/src/sensors/sensor_registry.cpp` — `validateI2cBusId()` helper replaces all 8 hardcoded `bus_id != 0` checks; error message now says "bus id N is not configured on this build"; all `default_i2c_bus_id = 0U` → `board::kPrimaryBus`; GPS validator message updated
  - `firmware/main/Kconfig.projbuild` — `AIR360_BOARD_PROFILE` choice (`V1`/`V2`); `I2C1_SDA/SCL_GPIO` options under `depends on BOARD_V2`; GPS and MH-Z19B UART port range widened to `1 9`
  - `docs/firmware/transport-binding.md` — fully rewritten to document board profiles, `BusConfig`, data-driven init, and uncapped UART ports
