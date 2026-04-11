# Air360 Firmware

This directory contains the ESP-IDF firmware project for Air360.

The current implementation is a Phase 3.2 runtime for `esp32s3` on ESP-IDF 6.x. It boots a C++17 application, initializes NVS and the ESP-IDF network core, loads or creates persisted device, sensor, and backend configuration, resolves whether the device should run in station mode or setup AP mode, synchronizes UTC time through SNTP when station uplink is available, exposes local HTTP endpoints at `/`, `/status`, `/config`, `/sensors`, and `/backends`, runs a background sensor manager for supported sensor drivers, and runs a background upload manager for supported remote backends.

Related implementation docs now live in [`../docs/firmware/`](../docs/firmware/).

If you need an end-user walkthrough for setup AP onboarding and the station-mode web UI, start with [`../docs/firmware/user-guide.md`](../docs/firmware/user-guide.md).

If you need to package a GitHub-release-ready firmware bundle from the current `build/` outputs, use the repo-local skill at [`../.agents/skills/air360-firmware-release-bundle/`](../.agents/skills/air360-firmware-release-bundle/).

## Project Structure

The firmware project root is `firmware/`.

Key files and directories:

- `CMakeLists.txt`
  ESP-IDF project root. The project name is `air360_firmware`.
- `main/`
  The application component built into the firmware image.
- `main/CMakeLists.txt`
  Registers the `main` component sources, embedded frontend assets, and required ESP-IDF components.
- `main/Kconfig.projbuild`
  Declares project-specific `CONFIG_AIR360_*` options exposed through `menuconfig`.
- `main/webui/`
  Hand-authored frontend assets and HTML templates embedded into the firmware image and used by the local web server.
- `sdkconfig.defaults`
  Repository defaults for important project settings.
- `sdkconfig`
  The full effective configuration for the current local build.
- `partitions.csv`
  Custom partition table used by this project.
- `firmware.code-workspace`
  VS Code workspace entry point for opening this directory as an ESP-IDF project.
- `main/third_party/`
  Vendored upstream sources used by sensor wrappers, currently including SPS30, TinyGPSPlus, Adafruit DHT, Adafruit VEML7700, the minimal Adafruit BusIO subset required by that driver, and a shared `arduino_compat/` shim layer reused by multiple Arduino-style upstream libraries.

### `main/`

The `main/` component is split into headers under `include/air360/` and implementations under `src/`.

### `main/include/air360/`

Public component headers for the current runtime:

- `app.hpp`
  Declares the top-level `air360::App` runner.
- `build_info.hpp`
  Defines `BuildInfo` and the helper that reads build metadata plus device identity fields used by the status layer.
- `config_repository.hpp`
  Defines the persisted `DeviceConfig` record and the NVS-backed repository API.
- `network_manager.hpp`
  Declares the station join flow, setup AP fallback, and reported network state.
- `status_service.hpp`
  Declares the service that renders the root HTML page and `/status` JSON.
- `web_assets.hpp`
  Declares embedded frontend asset lookup and stable asset href helpers for the firmware UI.
- `web_ui.hpp`
  Declares shared page-shell, notice, and HTML escaping helpers used by the server-rendered UI.
- `web_server.hpp`
  Declares the wrapper around `esp_http_server`, including config, sensor, and backend routes.
- `sensors/`
  Declares the sensor runtime types, registry, transport bindings, config model, and driver interfaces.
- `uploads/`
  Declares backend config persistence, upload transport, measurement queueing, backend registry, and uploader interfaces for remote integrations.

### `main/src/`

Current implementation files:

- `app_main.cpp`
  C entry point that constructs `air360::App` and calls `run()`.
- `app.cpp`
  Main startup flow: boot LEDs, watchdog, NVS, network core, config load/create, boot counter, sensor config load/create, network mode resolution, and HTTP server startup.
- `build_info.cpp`
  Reads project name, version, build date/time, ESP-IDF version, board name, and device identity fields.
- `config_repository.cpp`
  Stores and validates the `DeviceConfig` blob and boot counter in NVS.
- `network_manager.cpp`
  Attempts Wi-Fi station join from saved credentials, synchronizes SNTP time with `pool.ntp.org` when station uplink is available, and falls back to setup AP mode at `192.168.4.1`.
- `status_service.cpp`
  Produces the runtime HTML and JSON payloads, including build, config, network, sensor, and upload summaries, using the shared firmware UI shell.
- `web_assets.cpp`
  Maps embedded CSS and JavaScript assets to `/assets/*` requests.
- `web_ui.cpp`
  Provides shared page-shell rendering, embedded HTML template expansion, navigation, notices, and HTML escaping for firmware pages.
- `web_server.cpp`
  Starts `esp_http_server`, registers `/`, `/status`, `/config`, `/sensors`, `/backends`, `/wifi-scan`, and `/assets/*` handlers, stages sensor edits in memory until the user explicitly applies them live, and persists backend selection changes immediately.
- `webui/`
  Contains the embedded frontend files used by firmware, including shared CSS, progressive-enhancement JavaScript, and page body templates.
- `sensors/`
  Contains sensor persistence, registry, transport helpers, background orchestration, and concrete drivers.
- `uploads/`
  Contains backend persistence, runtime backend registry, in-memory measurement queueing, upload scheduling, HTTP transport, and concrete backend adapters for Sensor.Community and Air360 API.

## Requirements

Current project assumptions:

- target: `esp32s3`
- runtime: ESP-IDF 6.x
- build system: native ESP-IDF CMake

Recommended workflow is through the ESP-IDF VS Code extension, opening `firmware/` directly or opening `firmware/firmware.code-workspace`.

For terminal builds, the ESP-IDF environment must be loaded first so `IDF_PATH`, toolchain paths, and Python tooling are available.

## Configuration

This project uses compile-time defaults plus persisted runtime state.

### `main/Kconfig.projbuild`

This file defines the project-specific `CONFIG_AIR360_*` options:

- `CONFIG_AIR360_BOARD_NAME`
  Board label shown in build and status information.
- `CONFIG_AIR360_DEVICE_NAME`
  Default logical device name stored in the initial config record.
- `CONFIG_AIR360_HTTP_PORT`
  Port used by the status web server.
- `CONFIG_AIR360_I2C0_SDA_GPIO`
  Board-level I2C bus 0 SDA pin.
- `CONFIG_AIR360_I2C0_SCL_GPIO`
  Board-level I2C bus 0 SCL pin.
- `CONFIG_AIR360_GPS_DEFAULT_UART_PORT`
  Fixed UART port used by the GPS path.
- `CONFIG_AIR360_GPS_DEFAULT_RX_GPIO`
  Board-level GPS RX pin.
- `CONFIG_AIR360_GPS_DEFAULT_TX_GPIO`
  Board-level GPS TX pin.
- `CONFIG_AIR360_GPS_DEFAULT_BAUD_RATE`
  Default GPS baud rate.
- `CONFIG_AIR360_GPIO_SENSOR_PIN_0`
- `CONFIG_AIR360_GPIO_SENSOR_PIN_1`
- `CONFIG_AIR360_GPIO_SENSOR_PIN_2`
  The allowed board sensor pins used by GPIO-backed and analog-backed sensors. The current defaults are GPIO4, GPIO5, and GPIO6.
- `CONFIG_AIR360_ENABLE_LAB_AP`
  Controls whether setup AP defaults are enabled in the initial persisted config.
- `CONFIG_AIR360_LAB_AP_SSID`
  Default SSID for the setup AP.
- `CONFIG_AIR360_LAB_AP_PASSWORD`
  Default password for the setup AP.
- `CONFIG_AIR360_LAB_AP_CHANNEL`
  Compile-time Wi-Fi channel used by the AP.
- `CONFIG_AIR360_LAB_AP_MAX_CONNECTIONS`
  Compile-time max station count for the AP.

These options are consumed by the firmware through generated `CONFIG_*` macros. Some become defaults in the persisted `DeviceConfig`, some control board-level transport defaults, and AP channel and max connections are still read directly as compile-time settings.

### `sdkconfig.defaults`

This file provides repository defaults for a fresh configuration:

- flash size set to `16MB`
- custom partition table enabled via `partitions.csv`
- C++ exceptions disabled
- C++ RTTI disabled
- main task stack size increased to `8192`
- project defaults for the Air360 Kconfig options

This is the file to update when the project-wide default target or default runtime settings need to change.

### `sdkconfig`

This is the generated full configuration for the current local build. It includes:

- the selected ESP-IDF target
- all standard ESP-IDF options
- the resolved `CONFIG_AIR360_*` values
- the selected partition table and flash settings

Treat `sdkconfig` as the current effective build config, not as a concise source of project intent.

### Persisted runtime configuration

The runtime stores three separate NVS-backed models:

- `DeviceConfig`
  Device name, HTTP port, station credentials, setup AP credentials, and a stored local-auth flag that is not currently exposed in the UI.
- `SensorConfigList`
  The configured sensor inventory, including type, inferred transport, poll interval, and transport-specific fields.
- `BackendConfigList`
  Enabled backend set, upload interval, backend display names, static endpoint defaults, and backend-specific persisted fields used by individual upload adapters.

## Build

The canonical project root for commands is `firmware/`.

### VS Code workflow

1. Open `firmware/` directly in VS Code, or open `firmware/firmware.code-workspace`.
2. Make sure the ESP-IDF extension is pointed at your local ESP-IDF 6.x installation.
3. Confirm the target is `esp32s3`.
4. Run build from the ESP-IDF extension.

### Terminal workflow

Load the ESP-IDF environment, then build with `idf.py`:

```bash
cd firmware
. "$HOME/.espressif/v6.0/esp-idf/export.sh"
idf.py build
```

If you need to set the target on a fresh checkout or after resetting config:

```bash
cd firmware
. "$HOME/.espressif/v6.0/esp-idf/export.sh"
idf.py set-target esp32s3
idf.py build
```

Generated artifacts are written under `build/`. For the current project that includes:

- `build/air360_firmware.bin`
- `build/air360_firmware.elf`
- `build/air360_firmware.map`
- `build/compile_commands.json`

## Release Packaging

To package the current build for a GitHub beta or stable release, use the repo-local release skill script:

```bash
cd firmware
python3 .agents/skills/air360-firmware-release-bundle/scripts/create_release_bundle.py v0.1-beta.1
```

The script reads the current `build/` outputs and creates a versioned bundle under `release/air360-v<commit>/` with:

- `full/` merged image
- `split/` flashable ESP-IDF binaries plus `flash-offsets.txt`
- `air360-v<commit>-esp32s3-16mb-full.zip`
- `air360-v<commit>-esp32s3-16mb-split.zip`
- `release-notes.md`
- `sha256sums.txt`

The requested version string is used in `release-notes.md`. File and folder names are derived from the build's current commit-style project version from `build/project_description.json`.

## Flash

The current build artifacts target a 16 MB flash chip with the current partition table occupying the first part of flash:

- bootloader at `0x0`
- partition table at `0x8000`
- OTA data init at `0xf000`
- app image at `0x20000`

Typical flash command:

```bash
cd firmware
. "$HOME/.espressif/v6.0/esp-idf/export.sh"
idf.py -p /dev/tty.usbserial-0001 flash
```

If your serial device differs, replace the port accordingly.

## Monitor

Serial monitor baud for the current build is `115200`.

Use:

```bash
cd firmware
. "$HOME/.espressif/v6.0/esp-idf/export.sh"
idf.py -p /dev/tty.usbserial-0001 monitor
```

A common bring-up loop is:

```bash
cd firmware
. "$HOME/.espressif/v6.0/esp-idf/export.sh"
idf.py -p /dev/tty.usbserial-0001 flash monitor
```

After boot, the runtime exposes one of two local access paths:

- in setup AP mode:
  - `http://192.168.4.1/`
  - `http://192.168.4.1/config`
  - the UI intentionally redirects `/`, `/sensors`, and `/backends` to `/config`
  - `/wifi-scan` returns the scanned station SSID list used by the setup form
  - `/status` still exists as a JSON endpoint, but it is not linked from the AP-mode navigation
- in station mode:
  - the same routes are served on the DHCP address obtained by the device on the configured Wi-Fi network

Shared UI assets are served from `/assets/*`, currently including:

- `/assets/air360.css`
- `/assets/air360.js`

## Architecture Overview

The current startup flow is:

1. `app_main()` creates `air360::App`
2. `App::run()` initializes boot LEDs and arms the task watchdog
3. NVS is initialized
4. `esp_netif` and the default event loop are initialized
5. `ConfigRepository` loads or creates a `DeviceConfig` record
6. the boot counter is incremented in NVS
7. `SensorConfigRepository` loads or creates a `SensorConfigList`
8. `SensorManager` builds the managed sensor set and starts the `air360_sensor` FreeRTOS polling task when needed
9. `StatusService` is populated with build, config, network, and sensor runtime state
10. `BackendConfigRepository` loads or creates a `BackendConfigList`
11. `NetworkManager` attempts station join when Wi-Fi credentials are present
12. when station uplink is available, `NetworkManager` starts SNTP and waits for valid UTC system time
13. after startup, the main runtime loop keeps retrying SNTP synchronization in station mode until valid Unix time is available
14. if station config is missing or station join fails, `NetworkManager` starts setup AP mode at `192.168.4.1`
15. `UploadManager` starts, snapshots measurements from `MeasurementStore`, and schedules backend uploads
16. `WebServer` starts `esp_http_server` on the configured HTTP port

The firmware now has a clear central sensor orchestration model:

- one `SensorManager`
- one background polling task
- one runtime snapshot consumed by `/` and `/status`
- one generic measurement model used by all drivers through typed value channels rather than sensor-specific top-level structs

The firmware also has a separate upload pipeline:

- `SensorManager` appends measurement samples into `MeasurementStore`
- `MeasurementStore` maintains `pending` and `inflight` queues so uploads can be acknowledged or restored
- `UploadManager` only attempts upload when station uplink and valid Unix time are available
- `UploadManager` drains a bounded measurement window on each cycle rather than sending the whole queue at once
- when backlog remains after a successful upload, `UploadManager` temporarily shortens the next cycle to drain the queue faster
- `UploadTransport` executes HTTP requests and returns transport status, HTTP status, response size, and total request duration
- backend-specific adapters translate the generic measurement batch into backend payloads

Currently implemented backends are:

- `Sensor.Community`
  Fixed endpoint `http://api.sensor.community/v1/push-sensor-data/`
- `Air360 API`
  Fixed base endpoint `http://api.air360.ru` with dynamic route `/v1/devices/{chip_id}/batches/{batch_id}`

Backend selection and upload interval are configured through `/backends`. Endpoint URLs are static in firmware and are not edited through the UI.

For `Sensor.Community`, the `/backends` form also exposes a device id field prefilled from the runtime `Short ID`. You can change it for debugging; the saved value is then used for `X-Sensor` and related legacy id fields.

`Air360 API` no longer uses a bearer token in the current implementation. The backend UI only exposes enablement and the fixed endpoint description, and the uploader sends JSON without an `Authorization` header.

Supported drivers confirmed by the current registry:

- `BME280`
- `BME680`
- `SCD30`
- `VEML7700`
- `SPS30`
- `GPS (NMEA)`
- `DHT11`
- `DHT22`
- `ME3-NO2`

Current transport model by sensor type:

- `BME280`, `BME680`, `SCD30`, `VEML7700`, `SPS30`
  I2C sensors on bus 0, with board wiring from `CONFIG_AIR360_I2C0_*`.
- `GPS (NMEA)`
  UART sensor with fixed board wiring from `CONFIG_AIR360_GPS_DEFAULT_*`.

Current UI/runtime notes confirmed by the implementation:

- sensor poll interval minimum is `5000 ms`
- `Overview` now starts with a compact `Health` summary derived from time sync, sensor freshness, uplink availability, and backend health
- the `Sensors` page and `Overview` show queued sample counts per sensor based on `MeasurementStore`
- `Overview -> Sensors` shows the configured per-sensor poll interval
- `Overview -> Backends` shows the configured global upload interval for all backends
- `/status` includes both numeric `reset_reason` and string `reset_reason_label`
- `/status` also includes `health_status`, `health_summary`, and `health_checks`
- `DHT11`, `DHT22`, `ME3-NO2`
  Board-pin sensors restricted to the shared sensor pins from `CONFIG_AIR360_GPIO_SENSOR_PIN_{0,1,2}`. The selected sensor type determines whether the runtime uses GPIO or ADC.

Current default I2C addresses from the registry are:

- `BME280`: `0x76`
- `BME680`: `0x77`
- `SCD30`: `0x61`
- `VEML7700`: `0x10`
- `SPS30`: `0x69`

The `/sensors` page no longer asks the user to choose an arbitrary transport. Sensors are organized into categories (`Climate`, `Light`, `Particulate Matter`, `Location`, `Gas / CO2`), transport is inferred from the selected model, board-pin sensors expose only the allowed GPIO4/GPIO5/GPIO6 options, I2C sensors expose an optional I2C-address override, and UART sensors use the fixed bindings from the registry defaults. All categories except `Gas / CO2` currently allow only one configured sensor. Sensor edits are staged in memory until `Apply now` persists the staged list and rebuilds the sensor runtime without rebooting the device.

`GPS (NMEA)` currently reports latitude, longitude, altitude, satellites, speed, course, and HDOP through the generic `measurements` array.

The local web UI now uses a mixed frontend model:

- shared CSS, JavaScript, and page body templates live as standalone files under `main/webui/`
- those assets are embedded into the firmware image through `EMBED_TXTFILES`
- `WebServer` serves them from `/assets/*`
- page-specific data is still rendered server-side in C++, but the body markup now comes from embedded HTML templates
- `web_ui.cpp` provides a shared shell and template expansion layer so visual changes no longer require editing large inline HTML strings in every handler

Current UI behavior of note:

- `/config` only edits device name and station Wi-Fi credentials
- in setup AP mode the config page also shows a scanned SSID dropdown backed by `/wifi-scan`
- setup AP mode intentionally restricts the navigation to the `Device` section
- the runtime overview page starts with a compact `Health` block and then surfaces device identity, enabled backend summary, sensor summary, uptime, and boot count

## Storage and Partitions

The project uses a custom partition table in `partitions.csv`:

- `nvs`
  Used now for the persisted device config blob, sensor config blob, backend config blob, and boot counter.
- `otadata`
  Present for OTA metadata, but the current firmware does not implement OTA update logic yet.
- `phy_init`
  Standard ESP-IDF PHY calibration data partition.
- `factory`
  The current application image slot.
- `storage`
  A `spiffs` data partition reserved for future file-backed storage. The current runtime does not mount or use it yet.

The current runtime depends on NVS, not on SPIFFS.

## Debugging Notes

- If CMake complains about `/tools/cmake/project.cmake`, the ESP-IDF environment was not loaded and `IDF_PATH` is empty.
- If the build cannot find `xtensa-esp32s3-elf-gcc`, the ESP-IDF toolchain is not on `PATH`, which also points to a missing or incomplete ESP-IDF environment setup.
- For VS Code, open `firmware/` as the project root. Opening the repository root can cause non-ESP-IDF tooling to treat this as a plain CMake project instead.
- `build/compile_commands.json` is generated after a successful configure/build and is useful for editor integration.
- UTC timestamps depend on SNTP. The firmware waits for valid system time after successful station join before starting normal upload traffic.
- If Air360 HTTPS is re-enabled later, backend validation should continue to use the ESP-IDF certificate bundle rather than a firmware-pinned Air360 leaf certificate.

## Known Limitations

Current limitations confirmed by the source tree:

- no captive-portal DNS or wildcard DNS flow is implemented yet
- device config changes are still applied by reboot
- the local auth flag is stored but not enforced yet and is not currently exposed in the firmware UI
- the `storage` SPIFFS partition is reserved but not mounted or used
- the firmware UI is still server-rendered and form-driven rather than fully API-driven
- `Air360 API` currently defaults to plain HTTP because HTTPS from the ESP32-S3 shows unresolved connection-latency issues; TLS tuning and session-resumption work should be revisited later before switching the default back to HTTPS
