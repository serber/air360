# Air360 Firmware Architecture

## Current Purpose

The current firmware is a local runtime for an ESP32-S3-based Air360 device.

At the code level it currently does six main things:

- boots as a native ESP-IDF C++17 application
- initializes storage and networking
- persists device and sensor configuration in NVS
- exposes a local HTTP interface for device, sensor, and backend setup
- starts a background sensor manager that initializes and polls configured drivers
- starts a background upload manager that drains queued measurements to enabled remote backends

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
11. `UploadManager` starts with the current backend config and waits until station uplink, valid Unix time, and queued measurements are available before attempting uploads.
12. `WebServer` starts `esp_http_server` and registers `/`, `/status`, `/config`, `/sensors`, `/backends`, `/wifi-scan`, and `/assets/*`.
13. On successful startup the green LED is turned on. On early fatal startup failure the red LED is turned on.

## Runtime Model

The runtime uses a small number of long-lived service objects owned by [`App::run()`](../../firmware/main/src/app.cpp).

Long-lived services:

- `ConfigRepository`
- `StatusService`
- `SensorConfigRepository`
- `SensorManager`
- `NetworkManager`
- `UploadManager`
- `WebServer`

These are held in static storage inside `App::run()` rather than on the default ESP-IDF main task stack.

There are three important execution modes:

- startup logic runs synchronously in the main task
- sensor polling runs asynchronously in a dedicated FreeRTOS task created by `SensorManager`
- backend uploads run asynchronously in a dedicated FreeRTOS task created by `UploadManager`

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
- setup-AP Wi-Fi scan caching for `/wifi-scan`
- initial SNTP sync plus background station-mode retry until time becomes valid
- tracking the active `NetworkState`

### StatusService

[`../../firmware/main/src/status_service.cpp`](../../firmware/main/src/status_service.cpp)

Aggregates runtime state from build info, persisted config, network state, sensor manager snapshots, measurement store snapshots, and upload manager snapshots. It renders both the root HTML page and the `/status` JSON document. `/status` exposes a generic `measurements` array for every sensor plus a few convenience fields such as `temperature_c`, `humidity_percent`, `pressure_hpa`, and `gas_resistance_ohms` when those values exist.

The current `/status` payload also includes:

- `reset_reason` and `reset_reason_label`
- `health_status`, `health_summary`, and derived `health_checks`
- per-sensor `poll_interval_ms`
- per-sensor `queued_sample_count`

The root `Overview` page now also starts with a compact `Health` section derived from the same runtime inputs. The current implementation aggregates:

- time sync state
- enabled sensor reporting freshness
- station uplink availability
- enabled backend health

### UploadManager

[`../../firmware/main/src/uploads/upload_manager.cpp`](../../firmware/main/src/uploads/upload_manager.cpp)

Owns backend upload scheduling and queue draining:

- waits for active backend enablement, station uplink, and valid Unix time
- reads bounded upload windows from `MeasurementStore`
- skips empty-data cycles without treating them as backend errors
- retries failed sends by restoring the inflight queue
- drains backlog faster than the configured upload interval after successful sends when pending samples remain

### WebServer

[`../../firmware/main/src/web_server.cpp`](../../firmware/main/src/web_server.cpp)

Owns route registration and POST handling for:

- `/`
- `/status`
- `/config`
- `/sensors`
- `/backends`
- `/wifi-scan`
- `/assets/*`

It also saves configuration changes and triggers `esp_restart()` after a successful device config save. Sensor config changes are staged in memory inside `WebServer`; they are persisted only when the user explicitly applies them, and that action now rebuilds the sensor runtime without rebooting the device.

### SensorConfigRepository

[`../../firmware/main/src/sensors/sensor_config_repository.cpp`](../../firmware/main/src/sensors/sensor_config_repository.cpp)

Persists the sensor inventory in NVS under a dedicated blob. It validates the stored schema and replaces invalid or incompatible stored sensor config with defaults.

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
- stores lifecycle-oriented runtime state
- forwards successful sensor readings into `MeasurementStore`
- exposes runtime snapshots for the UI and `/status`

This is the current answer to the orchestration question: the firmware uses a single central manager with one background polling task rather than one task per sensor.

### MeasurementStore

[`../../firmware/main/src/uploads/measurement_store.cpp`](../../firmware/main/src/uploads/measurement_store.cpp)

Owns measurement runtime state:

- latest measurement snapshot for each sensor
- the bounded global pending queue
- the inflight upload window used by `UploadManager`

This means the UI-facing latest values and the upload queue no longer live inside `SensorManager`.

## HTTP And Local Control Surface

The runtime exposes local routes for overview, JSON status, device config, sensors, backends, and shared assets:

- `/`
  HTML status overview and links to config pages.
- `/status`
  JSON status payload with network, boot, config, and sensor runtime data, including a human-readable reset reason.
- `/config`
  Device and Wi-Fi configuration form.
- `/sensors`
  Sensor add/edit/delete flow plus current runtime sensor state. Sensor edits are staged in memory until `Apply now` persists them and rebuilds the sensor runtime live. The runtime cards expose configured poll interval and queued sample count.
- `/backends`
  Backend enablement, upload interval, and adapter-specific backend configuration exposed by the current UI. The overview page shows the configured global backend upload interval.
- `/wifi-scan`
  JSON endpoint exposing the cached setup-AP Wi-Fi scan list used by the `Device` page.
- `/assets/*`
  Shared embedded CSS and JavaScript used by the local web UI shell.

The local UI now uses a mixed model:

- page-specific data is still rendered server-side in C++
- shared CSS, JavaScript, and page templates live as standalone files under `firmware/main/webui/`
- those assets are embedded into the firmware image and served through `/assets/*`

The repository still keeps a separate planning note for the longer-term migration toward richer frontend assets and API-driven pages.

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
- measurement runtime ownership through `MeasurementStore`
- working drivers for selected I2C, GPIO, and UART sensors
- vendor-backed wrappers for Bosch BME280, Bosch BME680, ScioSense ENS160, Sensirion SPS30, TinyGPSPlus, and Adafruit DHT
- an `esp-idf-lib`-backed `VEML7700` wrapper that reports illuminance in lux
- a shared `third_party/arduino_compat/` shim layer reused by `ENS160` and `Adafruit_BusIO`
- a local ADC-backed `ME3-NO2` bring-up driver that currently reports raw ADC and calibrated millivolt readings for a custom analog AFE path
- a generic measurement model that allows different drivers to publish different channel sets
- local status reporting for live sensor state and measurements
- backend upload scheduling with bounded queue windows and backlog draining for `Sensor.Community` and `Air360 API`

Still clearly outside the current implementation:

- OTA update logic
- file-backed UI assets served from SPIFFS or embedded bundles
- authentication enforcement beyond the stored `local_auth_enabled` placeholder
- a captive portal or full onboarding UX beyond the current forms
