# Air360 Firmware Project Structure

## Purpose

This document explains how the ESP-IDF firmware project is laid out and how a contributor should navigate it.

The buildable project root is [`../../firmware/`](../../firmware/).

## Top-Level Layout

```text
firmware/
├── CMakeLists.txt
├── main/
│   ├── CMakeLists.txt
│   ├── Kconfig.projbuild
│   ├── include/air360/
│   ├── src/
│   └── third_party/
├── partitions.csv
├── sdkconfig
├── sdkconfig.defaults
├── README.md
└── firmware.code-workspace
```

## Entry Points And Build Metadata

- [`../../firmware/CMakeLists.txt`](../../firmware/CMakeLists.txt)
  ESP-IDF project root for `air360_firmware`.
- [`../../firmware/main/CMakeLists.txt`](../../firmware/main/CMakeLists.txt)
  Registers the `main` component, its C++ and vendor sources, and required ESP-IDF components.
- [`../../firmware/main/Kconfig.projbuild`](../../firmware/main/Kconfig.projbuild)
  Declares project-specific `CONFIG_AIR360_*` options exposed through `menuconfig`.
- [`../../firmware/sdkconfig.defaults`](../../firmware/sdkconfig.defaults)
  Repository defaults for target settings, partition selection, task stack, and board defaults.
- [`../../firmware/partitions.csv`](../../firmware/partitions.csv)
  Custom partition table used by the firmware image.

## Application Component Layout

### Core runtime

- [`../../firmware/main/src/app_main.cpp`](../../firmware/main/src/app_main.cpp)
  `app_main()` bridge into the C++ runtime.
- [`../../firmware/main/src/app.cpp`](../../firmware/main/src/app.cpp)
  Main boot sequence, LED state, NVS init, network selection, config loading, sensor startup, and web server start.
- [`../../firmware/main/src/build_info.cpp`](../../firmware/main/src/build_info.cpp)
  Build and device identity metadata.
- [`../../firmware/main/src/config_repository.cpp`](../../firmware/main/src/config_repository.cpp)
  NVS-backed `DeviceConfig` persistence and boot counter.
- [`../../firmware/main/src/network_manager.cpp`](../../firmware/main/src/network_manager.cpp)
  Wi-Fi station connect and setup AP fallback.
- [`../../firmware/main/src/status_service.cpp`](../../firmware/main/src/status_service.cpp)
  HTML and JSON status rendering.
- [`../../firmware/main/src/web_server.cpp`](../../firmware/main/src/web_server.cpp)
  `esp_http_server` wrapper for `/`, `/status`, `/config`, and `/sensors`.

### Public headers

Headers under [`../../firmware/main/include/air360/`](../../firmware/main/include/air360/) define the public C++ interfaces used inside the component:

- `App`
- `BuildInfo`
- `ConfigRepository`
- `NetworkManager`
- `StatusService`
- `WebServer`
- sensor subsystem interfaces under `sensors/`

### Sensor subsystem

The sensor subsystem lives under [`../../firmware/main/include/air360/sensors/`](../../firmware/main/include/air360/sensors/) and [`../../firmware/main/src/sensors/`](../../firmware/main/src/sensors/).

Core files:

- [`../../firmware/main/include/air360/sensors/sensor_types.hpp`](../../firmware/main/include/air360/sensors/sensor_types.hpp)
  Shared enums for sensor types, transports, runtime states, and measurement channels.
- [`../../firmware/main/include/air360/sensors/sensor_config.hpp`](../../firmware/main/include/air360/sensors/sensor_config.hpp)
  Persisted `SensorRecord` and `SensorConfigList`.
- [`../../firmware/main/include/air360/sensors/sensor_driver.hpp`](../../firmware/main/include/air360/sensors/sensor_driver.hpp)
  Driver interface plus generic measurement model.
- [`../../firmware/main/include/air360/sensors/sensor_registry.hpp`](../../firmware/main/include/air360/sensors/sensor_registry.hpp)
  Registry and descriptor model for supported sensors.
- [`../../firmware/main/src/sensors/sensor_config_repository.cpp`](../../firmware/main/src/sensors/sensor_config_repository.cpp)
  NVS-backed sensor configuration storage, including schema migration from v1 to v2.
- [`../../firmware/main/src/sensors/sensor_manager.cpp`](../../firmware/main/src/sensors/sensor_manager.cpp)
  Sensor orchestrator and background polling task.
- [`../../firmware/main/src/sensors/transport_binding.cpp`](../../firmware/main/src/sensors/transport_binding.cpp)
  Shared I2C and UART transport helpers for drivers, including the board wiring used by current sensor integrations.

Driver implementations are intentionally isolated under [`../../firmware/main/src/sensors/drivers/`](../../firmware/main/src/sensors/drivers/):

- `bme280_sensor.cpp`
- `bme680_sensor.cpp`
- `dht_sensor.cpp`
- `gps_nmea_sensor.cpp`
- `sps30_sensor.cpp`
- shared vendor-bridge helpers such as `bosch_i2c_support.cpp` and `sensirion_i2c_hal.cpp`

This layout is deliberate: adding a new sensor should usually mean adding one new driver plus one registry entry, not editing the whole runtime.

## Third-Party Driver Sources

Vendor source snapshots live under [`../../firmware/main/third_party/`](../../firmware/main/third_party/):

- `bme280/`
- `bme68x/`
- `sps30/`

These are compiled as part of the `main` component. Local wrapper classes keep the vendor APIs behind the firmware's own `SensorDriver` abstraction.

## HTTP Surface

The local web UI is currently generated in C++ rather than served from embedded static assets.

Confirmed routes:

- `/`
  Runtime overview page.
- `/status`
  JSON status payload with build, network, config, and sensor state.
- `/config`
  Device and Wi-Fi configuration form.
- `/sensors`
  Sensor configuration form for add, update, delete, and runtime inspection.

## Build Workflow

Recommended working roots:

- open [`../../firmware/`](../../firmware/) directly in VS Code
- or open [`../../firmware/firmware.code-workspace`](../../firmware/firmware.code-workspace)

Typical terminal workflow:

```bash
cd firmware
. "$HOME/.espressif/v6.0/esp-idf/export.sh"
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/tty.usbserial-0001 flash monitor
```

## How To Navigate As A Contributor

- Start in [`../../firmware/main/src/app.cpp`](../../firmware/main/src/app.cpp) to understand the runtime boot order.
- Then read [`../../firmware/main/src/web_server.cpp`](../../firmware/main/src/web_server.cpp) and [`../../firmware/main/src/status_service.cpp`](../../firmware/main/src/status_service.cpp) for the local control surface.
- For persistence, read [`../../firmware/main/src/config_repository.cpp`](../../firmware/main/src/config_repository.cpp) and [`../../firmware/main/src/sensors/sensor_config_repository.cpp`](../../firmware/main/src/sensors/sensor_config_repository.cpp).
- For sensors, read [`../../firmware/main/src/sensors/sensor_registry.cpp`](../../firmware/main/src/sensors/sensor_registry.cpp) before reading any concrete driver.
