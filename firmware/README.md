# Air360 Firmware

This directory contains the ESP-IDF firmware project for Air360.

The current implementation is a Phase 2 onboarding runtime for `ESP32-S3-DevKitC-1` on ESP-IDF 6.x. It boots a C++17 application, initializes NVS and the ESP-IDF network core, loads or creates a persisted device config record, resolves whether the device should run in station mode or setup AP mode, and exposes local HTTP endpoints at `/`, `/status`, and `/config`.

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
  Declares the wrapper around `esp_http_server`, including the config form route.

### `main/src/`

Current implementation files:

- `app_main.cpp`
  C entry point that constructs `air360::App` and calls `run()`.
- `app.cpp`
  Main startup flow: watchdog, NVS, event loop, config load/create, boot counter, network mode resolution, and HTTP server startup.
- `build_info.cpp`
  Reads project name, version, build date/time, ESP-IDF version, board name, and device identity fields.
- `config_repository.cpp`
  Stores and validates the `DeviceConfig` blob and boot counter in NVS.
- `network_manager.cpp`
  Attempts Wi-Fi station join from saved credentials and falls back to setup AP mode at `192.168.4.1`.
- `status_service.cpp`
  Produces the runtime HTML and JSON payloads, including build, config, and network summaries.
- `web_server.cpp`
  Starts `esp_http_server` and registers `/`, `/status`, and `/config` GET/POST handlers.

## Requirements

Current project assumptions:

- board: `ESP32-S3-DevKitC-1`
- target: `esp32s3`
- runtime: ESP-IDF 6.x
- build system: native ESP-IDF CMake

Recommended workflow is through the ESP-IDF VS Code extension, opening `firmware/` directly or opening `firmware/firmware.code-workspace`.

For terminal builds, the ESP-IDF environment must be loaded first so `IDF_PATH`, toolchain paths, and Python tooling are available.

## Configuration

This project uses three configuration layers:

### `main/Kconfig.projbuild`

This file defines the project-specific `CONFIG_AIR360_*` options:

- `CONFIG_AIR360_BOARD_NAME`
  Board label shown in build and status information.
- `CONFIG_AIR360_DEVICE_NAME`
  Default logical device name stored in the initial config record.
- `CONFIG_AIR360_HTTP_PORT`
  Port used by the status web server.
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

These options are consumed by the firmware through generated `CONFIG_*` macros. Some become defaults in the persisted `DeviceConfig`, while AP channel and max connections are currently read directly as compile-time settings.

### `sdkconfig.defaults`

This file provides repository defaults for a fresh configuration:

- flash size set to `4MB`
- custom partition table enabled via `partitions.csv`
- C++ exceptions disabled
- C++ RTTI disabled
- project defaults for the Air360 Kconfig options

This is the file to update when the project-wide default target or default runtime settings need to change.

### `sdkconfig`

This is the generated full configuration for the current local build. It includes:

- the selected ESP-IDF target
- all standard ESP-IDF options
- the resolved `CONFIG_AIR360_*` values
- the selected partition table and flash settings

Treat `sdkconfig` as the current effective build config, not as a concise source of project intent.

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
  - `http://192.168.4.1/status`
- in station mode:
  - the same routes are served on the DHCP address obtained by the device on the configured Wi-Fi network

## Architecture Overview

The current startup flow is:

1. `app_main()` creates `air360::App`
2. `App::run()` arms the task watchdog
3. NVS is initialized
4. `esp_netif` and the default event loop are initialized
5. `ConfigRepository` loads or creates a `DeviceConfig` record
6. the boot counter is incremented in NVS
7. `StatusService` is populated with build, config, and runtime state
8. `NetworkManager` attempts station join when Wi-Fi credentials are present
9. if station config is missing or station join fails, `NetworkManager` starts setup AP mode at `192.168.4.1`
10. `WebServer` starts `esp_http_server` on the configured HTTP port
11. `/` serves runtime status in station mode and redirects to `/config` in setup mode

This is still a small single-runtime design. There are no sensor drivers or uploader adapters yet, but the local onboarding and configuration flow is implemented in the firmware tree.

## Storage and Partitions

The project uses a custom partition table in `partitions.csv`:

- `nvs`
  Used now for the persisted device config blob and boot counter.
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

- no sensor readout path is implemented yet
- no backend upload logic is implemented yet
- no captive-portal DNS or wildcard DNS flow is implemented yet
- config changes are applied by reboot rather than live reconfiguration
- the local auth flag is stored but not enforced yet
- the `storage` SPIFFS partition is reserved but not mounted or used
- the project currently exposes only the minimal runtime pages `/`, `/status`, and `/config`
