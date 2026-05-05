# SHT3X

## Status

Implemented. Keep this document aligned with the current SHT3X driver and registry defaults.

## Scope

This document covers the Air360 SHT3X family driver, including default I2C binding and measurement behavior.

## Source of truth in code

- `firmware/main/src/sensors/drivers/sht3x_sensor.cpp`
- `firmware/main/include/air360/sensors/drivers/sht3x_sensor.hpp`
- `firmware/main/src/sensors/sensor_registry.cpp`

## Read next

- [README.md](README.md)
- [supported-sensors.md](supported-sensors.md)
- [../transport-binding.md](../transport-binding.md)

Temperature and humidity sensor from the Sensirion SHT30 / SHT31 / SHT35 family.

## Transport

- I2C, bus 0 (SDA=GPIO8, SCL=GPIO9)
- Default address: `0x44` (`ADDR` tied to GND)
- Alternate address: `0x45` (`ADDR` tied to VDD)
- Clock: 100 kHz, pull-ups enabled

## Initialization

1. Resolve bus pins via `context.i2c_bus_manager->resolvePins()`
2. Initialize the sensor descriptor via `sht3x_init_desc()` with the configured address
3. Initialize the sensor via `sht3x_init()`
4. Disable the heater via `sht3x_set_heater(false)`

## Polling

Each poll cycle:
1. Read both temperature and humidity in a single call via `sht3x_measure()`
2. Validate that no NaN values are returned

The high-level component call starts one single-shot high-repeatability measurement, waits for the conversion, and returns both values. The component documentation notes that this call may delay the calling task up to 30 ms, so it is used only from the sensor polling path.

## Measurements

| Measurement | ValueKind | Unit |
|-------------|-----------|------|
| Temperature | `kTemperatureC` | deg C |
| Humidity | `kHumidityPercent` | % |

## Notes

- The driver uses the `esp-idf-lib/sht3x` managed component.
- SHT3X supports two selectable I2C addresses. Air360 defaults to `0x44` and permits `0x45`.
- The built-in heater is explicitly disabled during initialization.
- The component's high-level `sht3x_measure()` call reads temperature and humidity as a single sample pair.
- If the measurement returns an error or NaN, the driver keeps short glitches local and marks the sensor for reinitialization after three consecutive poll failures.

## Recommended poll interval

Minimum 30 seconds.

## Component

`esp-idf-lib__sht3x` (managed component)

## Source files

- `firmware/main/src/sensors/drivers/sht3x_sensor.cpp`
- `firmware/main/include/air360/sensors/drivers/sht3x_sensor.hpp`
