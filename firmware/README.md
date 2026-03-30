# Air360 Firmware

This directory contains the ESP-IDF firmware project for Air360.

The current implementation is a Phase 3.2 runtime for `esp32s3` on ESP-IDF 6.x. It boots a C++17 application, initializes NVS and the ESP-IDF network core, loads or creates persisted device and sensor configuration, resolves whether the device should run in station mode or setup AP mode, exposes local HTTP endpoints at `/`, `/status`, `/config`, and `/sensors`, and runs a background sensor manager for supported sensor drivers.

Related implementation docs now live in [`../docs/firmware/`](../docs/firmware/).

## Project Structure

The firmware project root is `firmware/`.

Key files and directories:

- `CMakeLists.txt`
  ESP-IDF project root. The project name is `air360_firmware`.
- `main/`
  The application component built into the firmware image.
- `main/CMakeLists.txt`
  Registers the `main` component sources and required ESP-IDF components.
- `main/Kconfig.projbuild`
  Declares project-specific `CONFIG_AIR360_*` options exposed through `menuconfig`.
- `sdkconfig.defaults`
  Repository defaults for important project settings.
- `sdkconfig`
  The full effective configuration for the current local build.
- `partitions.csv`
  Custom partition table used by this project.
- `firmware.code-workspace`
  VS Code workspace entry point for opening this directory as an ESP-IDF project.
- `main/third_party/`
  Vendored upstream sources used by sensor wrappers, currently including BME280, BME680, ENS160, SPS30, TinyGPSPlus, and Adafruit DHT.

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
- `web_server.hpp`
  Declares the wrapper around `esp_http_server`, including config and sensor routes.
- `sensors/`
  Declares the sensor runtime types, registry, transport bindings, config model, and driver interfaces.

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
  Attempts Wi-Fi station join from saved credentials and falls back to setup AP mode at `192.168.4.1`.
- `status_service.cpp`
  Produces the runtime HTML and JSON payloads, including build, config, network, and sensor summaries.
- `web_server.cpp`
  Starts `esp_http_server`, registers `/`, `/status`, `/config`, and `/sensors` handlers, and stages sensor edits in memory until the user explicitly applies them and reboots.
- `sensors/`
  Contains sensor persistence, registry, transport helpers, background orchestration, and concrete drivers.

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

- flash size set to `4MB`
- custom partition table enabled via `partitions.csv`
- C++ exceptions disabled
- C++ RTTI disabled
- main task stack size increased to `6144`
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

The runtime stores two separate NVS-backed models:

- `DeviceConfig`
  Device name, HTTP port, station credentials, setup AP credentials, and local auth placeholder flag.
- `SensorConfigList`
  The configured sensor inventory, including type, inferred transport, poll interval, display name, and transport-specific fields.

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

## Flash

The current build artifacts show a 4 MB flash layout with:

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
  - `http://192.168.4.1/sensors`
  - `http://192.168.4.1/status`
- in station mode:
  - the same routes are served on the DHCP address obtained by the device on the configured Wi-Fi network

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
10. `NetworkManager` attempts station join when Wi-Fi credentials are present
11. if station config is missing or station join fails, `NetworkManager` starts setup AP mode at `192.168.4.1`
12. `WebServer` starts `esp_http_server` on the configured HTTP port

The firmware now has a clear central sensor orchestration model:

- one `SensorManager`
- one background polling task
- one runtime snapshot consumed by `/` and `/status`
- one generic measurement model used by all drivers through typed value channels rather than sensor-specific top-level structs

Supported drivers confirmed by the current registry:

- `BME280`
- `BME680`
- `SPS30`
- `ENS160`
- `GPS (NMEA)`
- `DHT11`
- `DHT22`
- `ME3-NO2`

Current transport model by sensor type:

- `BME280`, `BME680`, `SPS30`, `ENS160`
  I2C sensors on bus 0, with board wiring from `CONFIG_AIR360_I2C0_*`.
- `GPS (NMEA)`
  UART sensor with fixed board wiring from `CONFIG_AIR360_GPS_DEFAULT_*`.
- `DHT11`, `DHT22`, `ME3-NO2`
  Board-pin sensors restricted to the shared sensor pins from `CONFIG_AIR360_GPIO_SENSOR_PIN_{0,1,2}`. The selected sensor type determines whether the runtime uses GPIO or ADC.

Current default I2C addresses from the registry are:

- `BME280`: `0x77`
- `BME680`: `0x77`
- `SPS30`: `0x69`
- `ENS160`: `0x52`

The `/sensors` page no longer asks the user to choose an arbitrary transport. Transport is inferred from sensor type, board-pin sensors expose only the allowed GPIO4/GPIO5/GPIO6 options, and GPS uses the fixed UART binding for the board. Sensor edits are staged in memory until `Apply and reboot` persists the staged list and restarts the device.

## Storage and Partitions

The project uses a custom partition table in `partitions.csv`:

- `nvs`
  Used now for the persisted device config blob, sensor config blob, and boot counter.
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

## Known Limitations

Current limitations confirmed by the source tree:

- no backend upload logic is implemented yet
- no captive-portal DNS or wildcard DNS flow is implemented yet
- config changes are applied by reboot rather than live reconfiguration
- sensor changes are not applied live; they are staged in memory and only persisted when the user explicitly applies them and reboots
- the local auth flag is stored but not enforced yet
- the `storage` SPIFFS partition is reserved but not mounted or used
- the local UI is still assembled directly in C++ strings
