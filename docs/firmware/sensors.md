# Air360 Firmware Sensor Subsystem

## Purpose

This document explains how the current sensor subsystem works in `firmware/` and how supported drivers fit into the runtime.

The sensor subsystem is designed around one central orchestrator and multiple isolated driver implementations.

## Core Model

The subsystem is built from four layers:

- persisted sensor inventory in [`../../firmware/main/include/air360/sensors/sensor_config.hpp`](../../firmware/main/include/air360/sensors/sensor_config.hpp)
- driver interface and measurement model in [`../../firmware/main/include/air360/sensors/sensor_driver.hpp`](../../firmware/main/include/air360/sensors/sensor_driver.hpp)
- sensor registry in [`../../firmware/main/src/sensors/sensor_registry.cpp`](../../firmware/main/src/sensors/sensor_registry.cpp)
- runtime orchestration in [`../../firmware/main/src/sensors/sensor_manager.cpp`](../../firmware/main/src/sensors/sensor_manager.cpp)

## Central Orchestrator

The current firmware uses a single central orchestrator: [`SensorManager`](../../firmware/main/src/sensors/sensor_manager.cpp).

This class:

- reads the persisted sensor list supplied by `App`
- validates sensor records against `SensorRegistry`
- creates concrete driver instances
- calls `init()` during managed sensor construction
- starts one FreeRTOS task named `air360_sensor`
- iteratively calls `poll()` for active drivers
- stores the latest `SensorRuntimeInfo` snapshot for the UI and `/status`

The current design does not create one task per sensor.

## Measurement Abstraction

Measurements are generic rather than sensor-specific.

[`SensorMeasurement`](../../firmware/main/include/air360/sensors/sensor_driver.hpp) is a small collection of typed channel values:

- each sample has `sample_time_ms`
- each value is a `SensorValue { kind, value }`
- the meaning of each `kind` comes from [`SensorValueKind`](../../firmware/main/include/air360/sensors/sensor_types.hpp)

This lets different drivers publish different channel sets without changing the top-level status/UI model.

Examples:

- `BME280` publishes temperature, humidity, pressure
- `BME680` publishes temperature, humidity, pressure, and gas resistance
- `GPS (NMEA)` publishes latitude, longitude, altitude, satellites, and speed
- `SPS30` publishes PM mass, number concentration, and particle size channels

## Supported Sensor Types

The current registry defines these implemented sensor types:

- `bme280`
- `bme680`
- `sps30`
- `gps_nmea`
- `dht11`
- `dht22`

The authoritative list lives in [`../../firmware/main/src/sensors/sensor_registry.cpp`](../../firmware/main/src/sensors/sensor_registry.cpp).

## Transport Model

The current transport kinds are defined in [`../../firmware/main/include/air360/sensors/sensor_types.hpp`](../../firmware/main/include/air360/sensors/sensor_types.hpp):

- `i2c`
- `analog`
- `uart`
- `gpio`

Current transport usage by implemented drivers:

- `BME280`: I2C
- `BME680`: I2C
- `SPS30`: I2C
- `GPS (NMEA)`: UART
- `DHT11`: GPIO
- `DHT22`: GPIO

`analog` exists in the shared type model, but no analog driver is implemented yet.

## Board-Level Wiring Assumptions

### I2C

The current board defaults for I2C bus 0 come from `Kconfig`:

- SDA: GPIO8
- SCL: GPIO9

I2C-capable sensor defaults:

- `BME280`: bus 0, address `0x76`
- `BME680`: bus 0, address `0x76`
- `SPS30`: bus 0, address `0x69`

The registry accepts I2C bus ids `0` and `1`, but only bus 0 has explicit board wiring defaults in the current project config.

### UART / GPS

The GPS path is intentionally fixed to board wiring:

- UART port `1`
- RX GPIO44
- TX GPIO43
- default baud `9600`

The registry validates that GPS records match this fixed binding.

### GPIO-bound sensors

GPIO sensor slots are constrained to three board-level pins:

- GPIO4
- GPIO5
- GPIO6

The web UI exposes only those options for GPIO-based sensors.

## Driver Organization

Concrete drivers are isolated under [`../../firmware/main/src/sensors/drivers/`](../../firmware/main/src/sensors/drivers/).

Current patterns:

- Bosch-backed wrappers
  - `bme280_sensor.cpp`
  - `bme680_sensor.cpp`
  - shared Bosch I2C helper in `bosch_i2c_support.cpp`
- Sensirion-backed wrapper
  - `sps30_sensor.cpp`
  - shared Sensirion HAL bridge in `sensirion_i2c_hal.cpp`
- Native local drivers
  - `gps_nmea_sensor.cpp`
  - `dht_sensor.cpp`

Vendor source snapshots are compiled from [`../../firmware/main/third_party/`](../../firmware/main/third_party/).

## Web Configuration Flow

The `/sensors` page is implemented in [`../../firmware/main/src/web_server.cpp`](../../firmware/main/src/web_server.cpp).

Current behavior:

- list configured sensors
- add a new sensor from the registry
- edit an existing sensor
- delete a sensor
- infer the transport from the selected sensor type
- expose only valid board pin options for GPIO sensors
- apply fixed board UART wiring for GPS

After a successful save:

- `SensorConfigRepository` persists the new list
- `SensorManager::applyConfig()` rebuilds the active runtime state
- `StatusService` starts exposing the new configuration through `/` and `/status`

## Runtime States

Each sensor has a runtime state exposed through `SensorRuntimeInfo`:

- `disabled`
- `configured`
- `initialized`
- `polling`
- `absent`
- `unsupported`
- `error`

This lets the UI distinguish between:

- a sensor that is configured but has no driver
- a driver that initialized successfully
- a sensor that is being polled normally
- a device that is physically absent or currently failing

## Current Limitations

- There is no analog sensor driver yet, even though `analog` exists in the type model.
- Sensor config still stores several transport-specific fields directly in `SensorRecord`.
- The HTML UI is still server-rendered in C++.
- There is no dedicated upload pipeline yet; measurements are only exposed locally through the runtime pages and `/status`.
