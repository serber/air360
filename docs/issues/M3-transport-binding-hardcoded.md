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
