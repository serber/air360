# M2 — Positional aggregate initialization of `SensorDescriptor`

- **Severity:** Medium
- **Area:** Maintainability / correctness-under-change
- **Files:**
  - `firmware/main/src/sensors/sensor_registry.cpp` (14 entries)
  - `firmware/main/include/air360/sensors/sensor_registry.hpp` (`SensorDescriptor` definition)

## What is wrong

`SensorDescriptor` has ~14 fields. The registry initializes each entry positionally:

```cpp
SensorDescriptor{ "bme280", SensorType::kBme280, "BME280", I2cTransport,
                  /* default_i2c_address */ 0x76, ... };
```

Adding or reordering a field silently changes the meaning of every subsequent argument in all 14 entries.

## Why it matters

- Compiler accepts the miscategorized entries as long as types still match (several fields are all `uint8_t` or `const char*`).
- Bugs surface as sensors talking to wrong addresses or being mis-listed in the UI, discovered only at runtime.

## Consequences on real hardware

- One refactor that adds a field in the middle of the struct turns 14 sensors into 14 subtly wrong sensors.
- The bug may only affect some sensors (those where the swapped fields happen to produce a valid-looking value) — worst kind of latent defect.

## Fix plan

1. **Switch to C++20 designated initializers:**
   ```cpp
   SensorDescriptor{
       .key               = "bme280",
       .type              = SensorType::kBme280,
       .display_name      = "BME280",
       .transport         = Transport::kI2c,
       .default_address   = 0x76,
       .i2c_bus_id        = 0,
       .poll_interval_ms  = 5'000,
       // ...
   },
   ```
   Confirm the project is compiled as C++20 (ESP-IDF 6.0 supports it).
2. **If the project is still C++17,** either upgrade or use a small builder:
   ```cpp
   auto d = SensorDescriptor()
       .withKey("bme280")
       .withType(SensorType::kBme280)
       .withDisplayName("BME280")
       .withTransport(Transport::kI2c);
   ```
3. **Add a `static_assert` on the struct size** to catch accidental field additions in ABI-sensitive consumers.
4. **Grep for other positional aggregates in `main/`** and apply the same treatment to any struct with >4 fields.

## Verification

- Reordering fields in the struct produces compile errors at every entry (designated-initializer mismatch) — good, that's the point.
- All sensor descriptors still initialize identically at runtime (unit test: compare key, type, address for every registered sensor against a golden map).

## Related

- C1 — Sensirion HAL refactor may touch descriptor wiring; do M2 first to make the change safe.
