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
│   ├── webui/
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
  Registers the `main` component, its C++ and vendor sources, embedded frontend assets, and required ESP-IDF components.
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
- [`../../firmware/main/src/web_assets.cpp`](../../firmware/main/src/web_assets.cpp)
  Embedded web asset lookup and content-type mapping for CSS and JavaScript served by the firmware.
- [`../../firmware/main/src/web_ui.cpp`](../../firmware/main/src/web_ui.cpp)
  Shared page shell, embedded HTML template expansion, navigation, notices, and HTML escaping for the local web UI.
- [`../../firmware/main/src/web_server.cpp`](../../firmware/main/src/web_server.cpp)
  `esp_http_server` wrapper for `/`, `/status`, `/config`, `/sensors`, `/backends`, `/wifi-scan`, and `/assets/*`.
- [`../../firmware/main/webui/`](../../firmware/main/webui/)
  Hand-authored frontend files embedded directly into the firmware image, including `air360.css`, `air360.js`, and page body templates.

### Public headers

Headers under [`../../firmware/main/include/air360/`](../../firmware/main/include/air360/) define the public C++ interfaces used inside the component:

- `App`
- `BuildInfo`
- `ConfigRepository`
- `NetworkManager`
- `StatusService`
- `WebServer`
- `web_assets.hpp`
- `web_ui.hpp`
- sensor subsystem interfaces under `sensors/`
- upload subsystem interfaces under `uploads/`

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
  NVS-backed sensor configuration storage and schema validation for the current persisted sensor inventory.
- [`../../firmware/main/src/sensors/sensor_manager.cpp`](../../firmware/main/src/sensors/sensor_manager.cpp)
  Sensor orchestrator and background polling task.
- [`../../firmware/main/src/sensors/transport_binding.cpp`](../../firmware/main/src/sensors/transport_binding.cpp)
  Shared I2C and UART transport helpers for drivers, including the current `driver/i2c_master.h`-based I2C path and the board wiring used by sensor integrations.

Driver implementations are intentionally isolated under [`../../firmware/main/src/sensors/drivers/`](../../firmware/main/src/sensors/drivers/):

- `bme280_sensor.cpp`
- `bme680_sensor.cpp`
- `dht_sensor.cpp`
- `ens160_sensor.cpp`
- `gps_nmea_sensor.cpp`
- `me3_no2_sensor.cpp`
- `sps30_sensor.cpp`
- shared vendor-bridge helpers such as `bosch_i2c_support.cpp` and `sensirion_i2c_hal.cpp`

This layout is deliberate: adding a new sensor should usually mean adding one new driver plus one registry entry, not editing the whole runtime.

## Third-Party Driver Sources

Vendor source snapshots live under [`../../firmware/main/third_party/`](../../firmware/main/third_party/):

- `adafruit_dht/`
- `bme280/`
- `bme68x/`
- `ens160/`
- `sps30/`
- `tinygpsplus/`

These are compiled as part of the `main` component. Local wrapper classes keep the vendor APIs behind the firmware's own `SensorDriver` abstraction.

## HTTP Surface

The local web UI now uses a mixed model:

- page data is still rendered server-side in C++
- shared CSS, JavaScript, and page templates are embedded from `main/webui/`
- assets are served through a generic `/assets/*` route

Confirmed routes:

- `/`
  Runtime overview page. In setup AP mode the UI redirects this route to `/config`.
- `/status`
  JSON status payload with build, network, config, backend, and sensor state, including `reset_reason_label` and per-sensor queue depth.
- `/config`
  Device and Wi-Fi configuration form. In setup AP mode this page also consumes the scanned SSID list from `/wifi-scan`.
- `/sensors`
  Category-based sensor configuration page for add, update, delete, staged apply/discard, and runtime inspection. The runtime view now shows configured poll interval and queued sample count per sensor.
- `/backends`
  Backend configuration form for upload interval, enablement, and the current adapter-specific persisted fields exposed by the UI. The overview page also shows the configured global upload interval for all backend cards.
- `/wifi-scan`
  JSON endpoint returning the cached setup-AP Wi-Fi scan list.
- `/assets/*`
  Embedded CSS and JavaScript assets used by the shared firmware UI shell.

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
- Then read [`../../firmware/main/src/web_server.cpp`](../../firmware/main/src/web_server.cpp), [`../../firmware/main/src/status_service.cpp`](../../firmware/main/src/status_service.cpp), and [`../../firmware/main/src/web_ui.cpp`](../../firmware/main/src/web_ui.cpp) for the local control surface.
- For persistence, read [`../../firmware/main/src/config_repository.cpp`](../../firmware/main/src/config_repository.cpp) and [`../../firmware/main/src/sensors/sensor_config_repository.cpp`](../../firmware/main/src/sensors/sensor_config_repository.cpp).
- For sensors, read [`../../firmware/main/src/sensors/sensor_registry.cpp`](../../firmware/main/src/sensors/sensor_registry.cpp) before reading any concrete driver.
