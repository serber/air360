# AHT30

## Status

Implemented. Keep this document aligned with the current AHT30 driver and registry defaults.

## Scope

This document covers the Air360 AHT30 driver, including default I2C binding and measurement behavior.

## Source of truth in code

- `firmware/main/src/sensors/drivers/aht30_sensor.cpp`
- `firmware/main/include/air360/sensors/drivers/aht30_sensor.hpp`
- `firmware/main/src/sensors/sensor_registry.cpp`

## Read next

- [README.md](README.md)
- [supported-sensors.md](supported-sensors.md)
- [../transport-binding.md](../transport-binding.md)

Temperature and humidity sensor from ASAIR (Aosong Electronics).

## Transport

- I2C, bus 0 (SDA=GPIO8, SCL=GPIO9)
- Fixed address: `0x38` (no address selection pin)
- Clock: 100 kHz (inherited from the shared I2C bus)

## Initialization

1. Resolve the bus id via `context.i2c_bus_manager->getMasterBusHandle()` to obtain the `i2c_master_bus_handle_t`
2. Create the sensor handle via `aht30_create()` with the resolved bus handle and the configured I2C address

The AHT30 component uses the new ESP-IDF I2C master API. `getMasterBusHandle()` borrows the underlying handle already owned by `i2cdev` after `I2cBusManager::init()` runs.

## Polling

Each poll cycle:
1. Read temperature and humidity in a single call via `aht30_get_temperature_humidity_value()`
2. Validate that neither value is NaN

The component internally sends the measurement trigger command (`0xAC 0x33 0x00`), polls the busy flag up to 18 times (≈180 ms worst case), and returns both values.

## Measurements

| Measurement | ValueKind | Unit |
|-------------|-----------|------|
| Temperature | `kTemperatureC` | deg C |
| Humidity | `kHumidityPercent` | % |

## Notes

- The AHT30 has a single fixed I2C address (`0x38`). No address selection is available in hardware.
- The driver uses the `espressif/aht30` managed component (v1.0.0).
- The component requires `i2c_master_bus_handle_t` (new ESP-IDF I2C master API). The bus handle is obtained via `I2cBusManager::getMasterBusHandle()` which calls `i2c_master_get_bus_handle()` on the port already initialised by `i2cdev`.
- If the measurement returns an error or NaN, the driver keeps short glitches local and marks the sensor for reinitialization after three consecutive poll failures.

## Recommended poll interval

Minimum 30 seconds.

## Component

`espressif__aht30` (managed component, version 1.0.0)

## Source files

- `firmware/main/src/sensors/drivers/aht30_sensor.cpp`
- `firmware/main/include/air360/sensors/drivers/aht30_sensor.hpp`
