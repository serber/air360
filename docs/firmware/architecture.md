# Air360 Firmware Architecture

## Current Purpose

The current firmware is a local runtime for an ESP32-S3-based Air360 device.

At the code level it currently does five main things:

- boots as a native ESP-IDF C++17 application
- initializes storage and networking
- persists device and sensor configuration in NVS
- exposes a local HTTP interface for device setup and sensor setup
- starts a background sensor manager that initializes and polls configured drivers

The `docs/` folder contains broader project and compatibility planning, but this document describes only behavior confirmed by the `firmware/` source tree.

## Startup Sequence

The boot path is defined by [`../../firmware/main/src/app_main.cpp`](../../firmware/main/src/app_main.cpp) and [`../../firmware/main/src/app.cpp`](../../firmware/main/src/app.cpp).

Confirmed order:

1. `app_main()` constructs `air360::App` and calls `run()`.
2. `App::run()` configures the green and red boot LEDs on GPIO11 and GPIO10.
3. The main task watchdog is armed.
4. NVS is initialized. If NVS metadata is incompatible, the partition is erased and reinitialized.
5. `esp_netif` and the default event loop are initialized.
6. `ConfigRepository` loads or creates `DeviceConfig`.
7. Boot count is incremented in NVS.
8. `SensorConfigRepository` loads or creates `SensorConfigList`.
9. `SensorManager::applyConfig()` builds the in-memory runtime for configured sensors and starts the background polling task when at least one pollable driver exists.
10. `NetworkManager` attempts station mode when Wi-Fi credentials exist. Otherwise it starts setup AP mode.
11. `WebServer` starts `esp_http_server` and registers `/`, `/status`, `/config`, and `/sensors`.
12. On successful startup the green LED is turned on. On early fatal startup failure the red LED is turned on.

## Runtime Model

The runtime uses a small number of long-lived service objects owned by [`App::run()`](../../firmware/main/src/app.cpp).

Long-lived services:

- `ConfigRepository`
- `StatusService`
- `SensorConfigRepository`
- `SensorManager`
- `NetworkManager`
- `WebServer`

These are held in static storage inside `App::run()` rather than on the default ESP-IDF main task stack.

There are two important execution modes:

- startup logic runs synchronously in the main task
- sensor polling runs asynchronously in a dedicated FreeRTOS task created by `SensorManager`

The HTTP server task model is provided by `esp_http_server`, not by custom application task code.

## Main Modules

### App

[`../../firmware/main/src/app.cpp`](../../firmware/main/src/app.cpp)

Coordinates system bring-up and decides when the runtime is considered ready.

### ConfigRepository

[`../../firmware/main/src/config_repository.cpp`](../../firmware/main/src/config_repository.cpp)

Owns `DeviceConfig` validation, storage, default creation, and boot counter management in NVS.

### BuildInfo

[`../../firmware/main/src/build_info.cpp`](../../firmware/main/src/build_info.cpp)

Aggregates app metadata from ESP-IDF plus board label, detected chip model, chip revision, long chip id, short chip id, and MAC-derived identity data.

### NetworkManager

[`../../firmware/main/src/network_manager.cpp`](../../firmware/main/src/network_manager.cpp)

Owns Wi-Fi startup decisions:

- station connect from persisted credentials
- setup AP fallback
- tracking the active `NetworkState`

### StatusService

[`../../firmware/main/src/status_service.cpp`](../../firmware/main/src/status_service.cpp)

Aggregates runtime state from build info, persisted config, network state, and sensor manager snapshots. It renders both the root HTML page and the `/status` JSON document. `/status` exposes both a generic `measurements` array and a few convenience fields such as `temperature_c`, `humidity_percent`, `pressure_hpa`, and `gas_resistance_ohms` when those values exist.

### WebServer

[`../../firmware/main/src/web_server.cpp`](../../firmware/main/src/web_server.cpp)

Owns route registration and POST handling for:

- `/`
- `/status`
- `/config`
- `/sensors`

It also saves configuration changes and triggers `esp_restart()` after a successful device config save. Sensor config changes are applied live by persisting the updated list and calling `SensorManager::applyConfig()` without forcing a reboot.

### SensorConfigRepository

[`../../firmware/main/src/sensors/sensor_config_repository.cpp`](../../firmware/main/src/sensors/sensor_config_repository.cpp)

Persists the sensor inventory in NVS under a dedicated blob. It validates the stored schema and migrates v1 sensor records to v2.

### SensorRegistry

[`../../firmware/main/src/sensors/sensor_registry.cpp`](../../firmware/main/src/sensors/sensor_registry.cpp)

Defines the supported sensor types, their default parameters, their transport constraints, and their driver factory functions.

### SensorManager

[`../../firmware/main/src/sensors/sensor_manager.cpp`](../../firmware/main/src/sensors/sensor_manager.cpp)

Acts as the central orchestrator for sensors.

Responsibilities:

- builds managed sensor instances from persisted config
- initializes drivers
- owns the background task `air360_sensor`
- schedules iterative polling
- stores runtime state and latest measurements
- exposes snapshots for the UI and `/status`

This is the current answer to the orchestration question: the firmware uses a single central manager with one background polling task rather than one task per sensor.

## HTTP And Local Control Surface

The runtime exposes four local routes:

- `/`
  HTML status overview and links to config pages.
- `/status`
  JSON status payload with network, boot, config, and sensor runtime data.
- `/config`
  Device and Wi-Fi configuration form.
- `/sensors`
  Sensor add/edit/delete flow plus current runtime sensor state.

The HTML pages are still assembled directly in C++ strings. The repository has a separate planning note for later migration to embedded web assets, but that is not implemented yet.

## Error Handling Model

The current firmware is conservative:

- watchdog init failures are logged but are not fatal
- NVS init failure aborts startup
- network core init failure aborts startup
- config load failure falls back to in-memory defaults
- sensor config load failure falls back to an empty in-memory sensor list
- station connect failure falls back to setup AP
- web server start failure aborts startup
- individual sensor init or poll failures only change the runtime state of that sensor

Sensor failures are represented as `configured`, `initialized`, `polling`, `absent`, `unsupported`, or `error` through `SensorRuntimeState`.

## Implemented Versus Planned

Implemented in the current firmware:

- Phase 2-style onboarding through `/config`
- Phase 3 sensor configuration through `/sensors`
- background polling through `SensorManager`
- working drivers for selected I2C, GPIO, and UART sensors
- vendor-backed wrappers for Bosch BME280, Bosch BME680, and Sensirion SPS30
- a generic measurement model that allows different drivers to publish different channel sets
- local status reporting for live sensor state and measurements

Still clearly outside the current implementation:

- OTA update logic
- file-backed UI assets served from SPIFFS or embedded bundles
- uploader/backend compatibility logic
- authentication enforcement beyond the stored `local_auth_enabled` placeholder
- a captive portal or full onboarding UX beyond the current forms
