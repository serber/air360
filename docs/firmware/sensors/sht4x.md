# SHT4X

## Status

Implemented. Keep this document aligned with the current SHT4X driver and registry defaults.

## Scope

This document covers the Air360 SHT4X family driver, including default I2C binding and measurement behavior.

## Source of truth in code

- `firmware/main/src/sensors/drivers/sht4x_sensor.cpp`
- `firmware/main/include/air360/sensors/drivers/sht4x_sensor.hpp`
- `firmware/main/src/sensors/sensor_registry.cpp`

## Read next

- [README.md](README.md)
- [supported-sensors.md](supported-sensors.md)
- [../transport-binding.md](../transport-binding.md)

High-accuracy temperature and humidity sensor from Sensirion (SHT40 / SHT41 / SHT45 family).

## Transport

- I2C, bus 0 (SDA=GPIO8, SCL=GPIO9)
- Address: `0x44` (fixed for SHT4X)
- Clock: 100 kHz, pull-ups enabled

## Initialization

1. Resolve bus pins via `context.i2c_bus_manager->resolvePins()`
2. Initialize the sensor descriptor via `sht4x_init_desc()` (passes resolved port and GPIO pins)
3. Configure the sensor:
   - Repeatability: `SHT4X_HIGH` (maximum accuracy)
   - Heater: `SHT4X_HEATER_OFF`
4. Reset the sensor via `sht4x_reset()`

## Polling

Each poll cycle:
1. Read both temperature and humidity in a single call via `sht4x_measure()`
2. Validate that no NaN values are returned

Both values are acquired simultaneously in a single I2C transaction.

## Measurements

| Measurement | ValueKind | Unit |
|-------------|-----------|------|
| Temperature | `kTemperatureC` | °C |
| Humidity | `kHumidityPercent` | % |

## Notes

- `HIGH` repeatability mode maximizes accuracy at the cost of a slightly longer conversion time compared to `MEDIUM` and `LOW` modes.
- The built-in heater is disabled. The SHT4X heater can be used to evaporate condensation from the sensing element, but this is not implemented.
- Temperature and humidity are guaranteed to be captured at the same instant — a single `sht4x_measure()` call returns both.
- If the measurement returns an error or NaN, `initialized_` is reset.

## Recommended poll interval

Minimum 5 seconds.

## Component

`esp-idf-lib__sht4x` (managed component)

## Source files

- `firmware/main/src/sensors/drivers/sht4x_sensor.cpp`
- `firmware/main/include/air360/sensors/drivers/sht4x_sensor.hpp`
