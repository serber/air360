# Air360 Firmware Sensor Subsystem

## Purpose

This document explains how the current sensor subsystem works in `firmware/` and how supported drivers fit into the runtime.

The sensor subsystem is designed around one central orchestrator and multiple isolated driver implementations.

## Core Model

The subsystem is built from five layers:

- persisted sensor inventory in [`../../firmware/main/include/air360/sensors/sensor_config.hpp`](../../firmware/main/include/air360/sensors/sensor_config.hpp)
- driver interface and measurement model in [`../../firmware/main/include/air360/sensors/sensor_driver.hpp`](../../firmware/main/include/air360/sensors/sensor_driver.hpp)
- sensor registry in [`../../firmware/main/src/sensors/sensor_registry.cpp`](../../firmware/main/src/sensors/sensor_registry.cpp)
- runtime orchestration in [`../../firmware/main/src/sensors/sensor_manager.cpp`](../../firmware/main/src/sensors/sensor_manager.cpp)
- measurement runtime and upload queueing in [`../../firmware/main/src/uploads/measurement_store.cpp`](../../firmware/main/src/uploads/measurement_store.cpp)

## Central Orchestrator

The current firmware uses a single central orchestrator: [`SensorManager`](../../firmware/main/src/sensors/sensor_manager.cpp).

This class:

- reads the persisted sensor list supplied by `App`
- validates sensor records against `SensorRegistry`
- creates concrete driver instances
- calls `init()` during managed sensor construction
- starts one FreeRTOS task named `air360_sensor`
- iteratively calls `poll()` for active drivers
- stores lifecycle-oriented `SensorRuntimeInfo` snapshots for the UI and `/status`
- forwards successful readings into `MeasurementStore`

The current design does not create one task per sensor.

## Measurement Abstraction

Measurements are generic rather than sensor-specific.

[`SensorMeasurement`](../../firmware/main/include/air360/sensors/sensor_driver.hpp) is a small collection of typed channel values:

- each sample has `sample_time_ms`
- each value is a `SensorValue { kind, value }`
- the meaning of each `kind` comes from [`SensorValueKind`](../../firmware/main/include/air360/sensors/sensor_types.hpp)

This lets different drivers publish different channel sets without changing the top-level status/UI model.

The latest measurement payload is no longer owned by `SensorManager`. It is owned by the measurement runtime in `MeasurementStore`, which also owns the bounded upload queue.

Examples:

- `BME280` publishes temperature, humidity, pressure
- `BME680` publishes temperature, humidity, pressure, and gas resistance
- `ENS160` publishes AQI, TVOC, and eCO2
- `VEML7700` publishes ambient light in lux
- `GPS (NMEA)` publishes latitude, longitude, altitude, satellites, speed, course, and HDOP
- `DHT11` and `DHT22` publish temperature and humidity
- `ME3-NO2` publishes raw ADC and calibrated millivolt readings for a custom analog AFE path
- `SPS30` publishes PM mass, number concentration, and particle size channels

## Supported Sensor Types

The current registry defines these implemented sensor types. The authoritative list lives in [`../../firmware/main/src/sensors/sensor_registry.cpp`](../../firmware/main/src/sensors/sensor_registry.cpp).

The table below uses the same category semantics and ordering as the current `/sensors` UI.

| Category | Type Key | Transport | Reported Values | Default Binding |
| --- | --- | --- | --- | --- |
| `Climate` | `bme280` | `i2c` | `temperature`, `humidity`, `pressure` | bus 0, address `0x76` |
| `Climate` | `bme680` | `i2c` | `temperature`, `humidity`, `pressure`, `gas_resistance` | bus 0, address `0x77` |
| `Temperature / Humidity` | `dht11` | `gpio` | `temperature`, `humidity` | one of GPIO4, GPIO5, GPIO6; min poll `5000 ms` |
| `Temperature / Humidity` | `dht22` | `gpio` | `temperature`, `humidity` | one of GPIO4, GPIO5, GPIO6; min poll `5000 ms` |
| `Air Quality` | `ens160` | `i2c` | `aqi`, `tvoc`, `eco2` | bus 0, address `0x52` |
| `Light` | `veml7700` | `i2c` | `illuminance_lux` | bus 0, address `0x10` |
| `Particulate Matter` | `sps30` | `i2c` | PM mass, number concentration, particle size | bus 0, address `0x69` |
| `Location` | `gps_nmea` | `uart` | `latitude`, `longitude`, `altitude`, `satellites`, `speed`, `course`, `hdop` | UART1, RX GPIO44, TX GPIO43, default `9600` baud |
| `Gas` | `me3_no2` | `analog` | `adc_raw`, `voltage_mv` | one of GPIO4, GPIO5, GPIO6; default poll `5000 ms` |

## Board-Level Wiring Assumptions

### I2C

The current board defaults for I2C bus 0 come from `Kconfig`:

- SDA: GPIO8
- SCL: GPIO9

The current registry validates only bus 0 for I2C sensors, matching the only board wiring path implemented by the firmware.

### UART / GPS

The GPS path is intentionally fixed to board wiring:

- UART port `1`
- RX GPIO44
- TX GPIO43
- default baud `9600`

The registry validates that GPS records match this fixed binding.

### Shared sensor pins

Board sensor pins are constrained to three board-level pins:

- GPIO4
- GPIO5
- GPIO6

The web UI exposes only those options for sensor types that use board pins. The selected sensor type determines whether the runtime uses those pins through the GPIO path or the ADC path.

## Driver Organization

Concrete drivers are isolated under [`../../firmware/main/src/sensors/drivers/`](../../firmware/main/src/sensors/drivers/).

Current patterns:

- Adafruit-backed wrapper
  - `dht_sensor.cpp`
  - vendored driver under `third_party/adafruit_dht/`
- local ADC-backed driver
  - `me3_no2_sensor.cpp`
- Bosch-backed wrappers
  - `bme280_sensor.cpp`
  - `bme680_sensor.cpp`
  - shared Bosch I2C helper in `bosch_i2c_support.cpp`
- Sensirion-backed wrapper
  - `sps30_sensor.cpp`
  - shared Sensirion HAL bridge in `sensirion_i2c_hal.cpp`
- ScioSense-backed wrapper
  - `ens160_sensor.cpp`
  - vendored driver under `third_party/ens160/`
- Adafruit-backed ambient light wrapper
  - `veml7700_sensor.cpp`
  - vendored driver under `third_party/adafruit_veml7700/`
- TinyGPSPlus-backed wrapper
  - `gps_nmea_sensor.cpp`
  - vendored parser under `third_party/tinygpsplus/`

Vendor source snapshots are compiled from [`../../firmware/main/third_party/`](../../firmware/main/third_party/).

The shared I2C transport is implemented in [`../../firmware/main/src/sensors/transport_binding.cpp`](../../firmware/main/src/sensors/transport_binding.cpp) on top of ESP-IDF's `driver/i2c_master.h` API through `esp_driver_i2c`.

## Web Configuration Flow

The `/sensors` page is implemented in [`../../firmware/main/src/web_server.cpp`](../../firmware/main/src/web_server.cpp).

Current behavior:

- organize sensors by category instead of showing one flat driver list
- categories are currently `Climate`, `Temperature / Humidity`, `Air Quality`, `Light`, `Particulate Matter`, `Location`, and `Gas`
- treat every category except `Gas` as a single-sensor slot
- add a new sensor from the models allowed in that category
- edit an existing sensor
- delete a sensor
- infer the transport from the selected sensor type
- show transport as derived board wiring rather than an editable free-form choice
- expose an I2C address override field for I2C-backed sensors
- expose only valid board pin options for GPIO-backed and analog-backed sensors
- apply fixed board UART wiring for GPS
- keep edits in a staged in-memory `SensorConfigList`
- persist staged changes only when the user explicitly applies them
- allow discarding the staged list without touching persisted NVS state

The current category-to-model mapping is:

- `Climate`
  - `BME280`
  - `BME680`
- `Temperature / Humidity`
  - `DHT11`
  - `DHT22`
- `Air Quality`
  - `ENS160`
- `Light`
  - `VEML7700`
- `Particulate Matter`
  - `SPS30`
- `Location`
  - `GPS (NMEA)`
- `Gas`
  - `ME3-NO2`

After `Apply now`:

- `SensorConfigRepository` persists the new list
- `SensorManager::applyConfig()` rebuilds the active runtime state without rebooting the device
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

The runtime snapshot exposed through `Overview`, `/sensors`, and `/status` also includes:

- configured `poll_interval_ms`
- latest measurement values derived from `MeasurementStore`
- `queued_sample_count` derived from `MeasurementStore`

`Overview` also derives a top-level `Health` summary from sensor freshness, time sync, network uplink, and backend state, but the per-sensor runtime source of truth remains the snapshot above.

## Current Limitations

- Sensor config still stores several transport-specific fields directly in `SensorRecord`.
- The HTML UI is still server-rendered in C++.
- The sensor docs here focus on the local sensor/runtime path. Upload behavior is implemented separately through the measurement store and backend adapters.
- Only I2C bus 0 is currently supported by the board wiring and runtime validation path.
