# Air360 Firmware Architecture

## Purpose

This document describes the firmware implementation in `firmware/`.

The current firmware is a Phase 2 ESP-IDF onboarding runtime for `ESP32-S3-DevKitC-1`. Its purpose today is still narrow:

- boot cleanly as a native ESP-IDF C++17 application
- initialize core platform services
- persist a small device configuration record in NVS
- expose a recoverable setup AP mode at `192.168.4.1`
- provide a minimal local configuration and status surface at `/`, `/status`, and `/config`

The broader replacement-firmware goals for `sensor.community` exist in `../docs/`, but this document treats `firmware/` as the source of truth for what is implemented now.

## Repository Context

This architecture document is scoped to `firmware/`.

Related documents in `../docs/` describe the intended long-term architecture, compatibility requirements, and phased roadmap. They are useful context, but they should not be read as proof that the features are already present in the firmware tree.

## Firmware Project Layout

```text
firmware/
├── CMakeLists.txt
├── main/
│   ├── CMakeLists.txt
│   ├── Kconfig.projbuild
│   ├── include/
│   │   └── air360/
│   └── src/
├── partitions.csv
├── sdkconfig
├── sdkconfig.defaults
├── README.md
└── ARCHITECTURE.md
```

The most important implementation areas are:

- `CMakeLists.txt`
  ESP-IDF project root for `air360_firmware`.
- `main/CMakeLists.txt`
  Declares the application component and its ESP-IDF dependencies.
- `main/include/air360/`
  Public headers for the current runtime modules.
- `main/src/`
  The actual runtime implementation.
- `main/Kconfig.projbuild`
  Project-defined configuration surface.
- `sdkconfig.defaults`
  Repository defaults for target, partition table, and project options.
- `sdkconfig`
  The generated effective configuration for the current local build.
- `partitions.csv`
  The custom flash partition table.

## Startup Sequence

The startup flow currently confirmed by the source tree is:

1. ESP-IDF calls `app_main()` in `main/src/app_main.cpp`.
2. `app_main()` constructs `air360::App` and calls `run()`.
3. `App::run()` arms the task watchdog through `esp_task_wdt_init()` and `esp_task_wdt_add(nullptr)`.
4. NVS is initialized with `nvs_flash_init()`.
5. If NVS reports `ESP_ERR_NVS_NO_FREE_PAGES` or `ESP_ERR_NVS_NEW_VERSION_FOUND`, the firmware erases the NVS partition and retries initialization.
6. The networking core is initialized through `esp_netif_init()` and `esp_event_loop_create_default()`.
7. A `ConfigRepository` instance attempts to load a stored `DeviceConfig` blob from NVS.
8. If no config exists, or if the stored config is invalid, default config values are generated from compile-time `CONFIG_AIR360_*` settings and written to NVS.
9. The boot counter is read, incremented, and committed in NVS.
10. `StatusService` is constructed and populated with build metadata, config state, boot count, and later network state.
11. If saved station credentials are present, `NetworkManager` attempts Wi-Fi station join.
12. If station config is missing or the join attempt fails, `NetworkManager` starts setup AP mode at `192.168.4.1`.
13. `WebServer` starts `esp_http_server` on the configured HTTP port and registers `/`, `/status`, and `/config`.
14. The startup task removes itself from the watchdog and then stays alive in a low-activity loop using `vTaskDelay()`.

Startup failures are handled conservatively:

- watchdog setup failures log a warning and the firmware continues
- NVS init failure aborts startup
- network core init failure aborts startup
- web server start failure aborts startup
- config load failure falls back to in-memory defaults
- station join failure logs a warning and the firmware falls back to setup AP mode
- setup AP start failure logs a warning and the firmware continues toward web server startup

## Runtime Model

The current runtime is simple and mostly startup-driven.

- There is one explicit top-level application object, `air360::App`.
- No project-defined FreeRTOS tasks, queues, timers, or event buses are created in the firmware code.
- After startup, the firmware relies on long-lived service objects held on the stack inside `App::run()`.
- The top-level task idles in an infinite `vTaskDelay()` loop after services are started.
- The HTTP interface is handled by `esp_http_server` after `httpd_start()`. The server tasking model is provided by ESP-IDF rather than custom application tasks.

In practice, this means the current firmware behaves like a small synchronous bootstrap that hands off steady-state request handling to ESP-IDF services.

## Components and Responsibilities

### App

**Responsibility**  
Coordinates the complete startup flow and owns the lifetime of the main runtime services.

**Key files**  
`main/include/air360/app.hpp`  
`main/src/app.cpp`  
`main/src/app_main.cpp`

**Inputs**  
Compile-time configuration, NVS state, ESP-IDF platform services.

**Outputs**  
Initialized runtime services, boot logs, and the long-lived main loop.

**Dependencies**  
`ConfigRepository`, `StatusService`, `NetworkManager`, `WebServer`, NVS, `esp_netif`, event loop, watchdog APIs.

**Concurrency model**  
Runs synchronously in the main startup context.

### ConfigRepository

**Responsibility**  
Defines the persisted `DeviceConfig` record, validates stored config data, loads or creates the config blob, and maintains a boot counter.

**Key files**  
`main/include/air360/config_repository.hpp`  
`main/src/config_repository.cpp`

**Inputs**  
Generated `CONFIG_AIR360_*` defaults, stored NVS values in namespace `air360`.

**Outputs**  
Validated `DeviceConfig`, boot count, updated NVS state.

**Dependencies**  
NVS APIs such as `nvs_open`, `nvs_get_blob`, `nvs_set_blob`, `nvs_get_u32`, `nvs_set_u32`, `nvs_commit`.

**Concurrency model**  
Synchronous startup-time logic; no background task.

### BuildInfo

**Responsibility**  
Reads build metadata from the app image and combines it with the configured board name plus device identity fields.

**Key files**  
`main/include/air360/build_info.hpp`  
`main/src/build_info.cpp`

**Inputs**  
ESP-IDF app description metadata, `CONFIG_AIR360_BOARD_NAME`, efuse MAC, and station MAC.

**Outputs**  
A `BuildInfo` struct consumed by the status layer.

**Dependencies**  
`esp_app_get_description()`, `esp_get_idf_version()`.

**Concurrency model**  
Synchronous helper called during startup.

### NetworkManager

**Responsibility**  
Resolves Wi-Fi mode at boot, attempts station join from saved credentials, falls back to setup AP mode, and reports the resulting network state.

**Key files**  
`main/include/air360/network_manager.hpp`  
`main/src/network_manager.cpp`

**Inputs**  
Loaded `DeviceConfig`, compile-time AP channel and max-connection settings.

**Outputs**  
Wi-Fi mode, station attempt/result, AP state, SSID summary, last error, and IP address in `NetworkState`.

**Dependencies**  
`esp_netif`, `esp_wifi`, lwIP IPv4 helpers.

**Concurrency model**  
Synchronous startup logic. Ongoing Wi-Fi operation is then managed by ESP-IDF internals.

### StatusService

**Responsibility**  
Aggregates build, config, boot, and network state and renders the current local status payloads.

**Key files**  
`main/include/air360/status_service.hpp`  
`main/src/status_service.cpp`

**Inputs**  
`BuildInfo`, `DeviceConfig`, boot count, network state, reset reason, uptime.

**Outputs**  
Root HTML response and `/status` JSON response.

**Dependencies**  
Standard C++ string handling plus `esp_timer_get_time()` and `esp_reset_reason()`.

**Concurrency model**  
Passive state holder used by HTTP request handlers.

### WebServer

**Responsibility**  
Starts the HTTP server and binds the current local interface.

**Key files**  
`main/include/air360/web_server.hpp`  
`main/src/web_server.cpp`

**Inputs**  
`StatusService`, `ConfigRepository`, the in-memory `DeviceConfig`, and the configured HTTP port.

**Outputs**  
The `/` HTML endpoint, `/status` JSON endpoint, and `/config` GET/POST flow.

**Dependencies**  
`esp_http_server`.

**Concurrency model**  
Startup config is synchronous; request servicing is handled through ESP-IDF HTTP server callbacks.

## Configuration Model

The firmware uses both compile-time and persisted runtime configuration.

### Compile-time configuration

Project-specific options are declared in `main/Kconfig.projbuild` and materialize as `CONFIG_AIR360_*` macros in `sdkconfig`.

The current project-defined options are:

- board name
- default device name
- HTTP status server port
- lab AP enable switch
- lab AP SSID
- lab AP password
- lab AP channel
- lab AP max connections

`sdkconfig.defaults` defines the repository defaults for those values and also fixes a few project-level build assumptions:

- target flash size `4MB`
- custom partition table enabled
- C++ exceptions disabled
- C++ RTTI disabled

### Persisted runtime configuration

The runtime configuration stored in NVS is `DeviceConfig`, which currently contains:

- record header fields for magic, schema version, and record size
- HTTP port
- `lab_ap_enabled`
- `local_auth_enabled`
- device name
- Wi-Fi station SSID
- Wi-Fi station password
- lab AP SSID
- lab AP password

This creates an important split:

- compile-time settings in `sdkconfig` define the default shape and some fixed parameters
- runtime settings in NVS hold the mutable device record used during boot

At the moment, not every Kconfig option is persisted. In particular:

- AP SSID and password become part of `DeviceConfig`
- AP channel and max connections remain compile-time values read directly from `CONFIG_AIR360_*`

So the configuration model is currently hybrid rather than fully runtime-driven.

## Storage and Partitions

The custom partition table is:

- `nvs` at `0x9000`, size `0x6000`
- `otadata` at `0xf000`, size `0x2000`
- `phy_init` at `0x11000`, size `0x1000`
- `factory` app slot at `0x20000`, size `1536K`
- `storage` as a `spiffs` data partition at `0x1a0000`, size `0x60000`

### NVS usage

NVS is the only partition actively used by the current firmware code.

The namespace is `air360`, with two stored keys:

- `device_cfg`
  The binary `DeviceConfig` blob.
- `boot_count`
  A monotonically incremented boot counter.

The firmware validates the loaded config for:

- expected magic
- expected schema version
- expected record size
- non-zero HTTP port
- valid string termination
- Wi-Fi station SSID length
- Wi-Fi station password length
- SSID length
- password length

If the stored config is missing, malformed, or schema-incompatible, defaults are rewritten.

### OTA and app slots

An `otadata` partition exists, but the current firmware tree does not implement OTA update logic. There is only one app partition, `factory`, so the current layout is not a dual-slot OTA architecture.

### File-backed storage

A `storage` partition exists with `spiffs` subtype, but the current firmware does not mount SPIFFS or read/write files there. This appears reserved for later work rather than active runtime behavior.

## Hardware Integration

Current hardware assumptions visible in the code are minimal and specific:

- target board is `ESP32-S3-DevKitC-1`
- Wi-Fi AP mode is used for the current lab bring-up path
- AP IPv4 address is fixed to `192.168.4.1/24`

What is not currently visible in the firmware code:

- GPIO ownership
- I2C, SPI, UART, ADC, or other peripheral buses
- sensor drivers
- display handling
- cellular or LoRa integration

So the present firmware is a network-and-status foundation rather than a hardware-integration-heavy application.

## External Interfaces

The current firmware exposes only local interfaces.

### Local HTTP

Implemented endpoints:

- `/`
  In setup AP mode this redirects to `/config`. In station mode it renders a small HTML runtime page with boot, config, and network summary.
- `/status`
  A JSON status payload with build, reset, config, network, and uptime information.
- `/config`
  A local HTML configuration form with GET/POST handlers. It stores Wi-Fi credentials and basic device settings in NVS, then reboots the device.

### Wi-Fi

If station credentials are configured, the firmware tries station mode first. If credentials are missing or station join fails, it starts a setup AP using the configured SSID and password and serves the local HTTP endpoints there.

### What is not implemented yet

There is no confirmed implementation in `firmware/` for:

- backend uploads
- wildcard DNS or captive portal DNS behavior
- OTA update flows
- sensor sampling interfaces

Those areas are discussed in `../docs/`, but they are not active runtime behavior in this firmware tree.

## Logging and Error Handling

The code uses ESP-IDF logging with module-local tags:

- `air360.app`
- `air360.config`
- `air360.net`
- `air360.web`

The logging style is straightforward:

- informational logs for the startup sequence
- warnings for recoverable failures or degraded operation
- errors for failures that abort startup

Error-handling behavior by area:

- watchdog init failure logs a warning and startup continues
- NVS init failure aborts startup
- invalid or missing config is repaired by rewriting defaults
- boot counter failures log a warning and startup continues
- station join failures log a warning and trigger AP fallback
- setup AP failures log a warning and startup continues
- web server startup failure aborts startup

The overall pattern is fail-fast for mandatory platform services and degrade-when-possible for optional bring-up steps.

## Implemented vs Planned

### Confirmed in implementation

- native ESP-IDF CMake project targeting `esp32s3`
- C++17 application with explicit modules under `air360`
- startup watchdog initialization
- NVS-based config and boot counter persistence
- network core initialization with `esp_netif` and the default event loop
- station-mode join attempt using persisted Wi-Fi credentials
- setup AP fallback path at `192.168.4.1`
- local HTTP endpoints `/`, `/status`, and `/config`
- status reporting of build metadata, reset reason, boot count, config summary, and network state

### Inferred from structure

- the current design is intended to grow by adding more bounded modules rather than expanding one monolithic source file
- the `storage` SPIFFS partition is being reserved for later file-backed data
- the existing `DeviceConfig` header fields are intended to support controlled schema evolution

### Planned or described in docs

- wildcard-DNS captive onboarding around `/config`
- legacy compatibility adapters for server uploads
- wider sensor support
- richer runtime architecture around connectivity, normalization, and uploads
- later transport expansion beyond the current local AP bring-up path

## Open Questions

- Should AP channel and max connections remain compile-time settings, or move into persisted runtime config like SSID and password?
- Will `storage` be used for UI assets, an outbox, or some other file-backed data model?
- When OTA work begins, will the partition table move from a single `factory` slot to a dual-app layout?
- What should the long-term boundary be between lab bring-up behavior and the intended production onboarding flow?
- Which future modules belong in the `main` component versus separate ESP-IDF components as the firmware grows?
