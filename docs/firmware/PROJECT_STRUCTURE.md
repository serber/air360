# Air360 Firmware — Project Structure

## Status

Implemented. Keep this document aligned with the current `firmware/` tree.

## Scope

This document explains how the firmware project is laid out on disk and which directories and source files own each part of the runtime.

## Source of truth in code

- `firmware/CMakeLists.txt`
- `firmware/main/CMakeLists.txt`
- `firmware/main/include/air360/`
- `firmware/main/src/`

## Read next

- [ARCHITECTURE.md](ARCHITECTURE.md)
- [startup-pipeline.md](startup-pipeline.md)
- [change-impact-map.md](change-impact-map.md)

## Purpose

This document explains how the ESP-IDF firmware project is laid out and how a contributor should navigate it.

The buildable project root is `firmware/`.

---

## Top-level layout

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
├── managed_components/
├── partitions.csv
├── sdkconfig
├── sdkconfig.defaults
└── firmware.code-workspace
```

---

## Entry points and build metadata

- `CMakeLists.txt` — ESP-IDF project root for `air360_firmware`
- `main/CMakeLists.txt` — registers the `main` component: sources, embedded frontend assets, required ESP-IDF components
- `main/Kconfig.projbuild` — project-specific `CONFIG_AIR360_*` options exposed through `menuconfig`
- `sdkconfig.defaults` — repository defaults for target, partition table, task stack, and board pins
- `partitions.csv` — custom partition table (nvs, otadata, phy_init, factory, storage)
- `managed_components/` — ESP-IDF component manager dependencies (bme280, bme680, dht, ds18b20, scd30, sht4x, si7021, veml7700, tinygpsplusplus, esp_modem, led_strip, onewire_bus, i2c_bus)

---

## Application component layout

### Core runtime

- `main/src/app_main.cpp` — `app_main()` entry, constructs and runs `air360::App`
- `main/src/app.cpp` — 9-step boot sequence: LEDs, watchdog, NVS, network core, config, sensors, uploads, web server
- `main/src/build_info.cpp` — build metadata and device identity (chip_id, short_chip_id, esp_mac_id)
- `main/src/config_repository.cpp` — NVS-backed `DeviceConfig` persistence and boot counter
- `main/src/network_manager.cpp` — Wi-Fi station connect, SNTP, and setup AP fallback at `192.168.4.1`
- `main/src/cellular_config_repository.cpp` — NVS-backed `CellularConfig` persistence (independent schema version)
- `main/src/cellular_manager.cpp` — SIM7600E modem lifecycle: PPP session, reconnect task, exponential backoff, hardware reset
- `main/src/connectivity_checker.cpp` — post-PPP ICMP ping check via `esp_ping`
- `main/src/modem_gpio.cpp` — PWRKEY / SLEEP / RESET GPIO helpers for the SIM7600E
- `main/src/status_service.cpp` — HTML rendering for `/` and `/diagnostics`, plus raw status JSON generation for the diagnostics page
- `main/src/web_assets.cpp` — embedded CSS/JS asset lookup and content-type mapping
- `main/src/web_ui.cpp` — shared page shell, HTML template expansion, navigation, and HTML escaping
- `main/src/web_server.cpp` — `esp_http_server` routes: `/`, `/diagnostics`, `/config`, `/sensors`, `/backends`, `/wifi-scan`, `/check-sntp`, `/assets/*`
- `main/webui/` — hand-authored frontend assets embedded into the firmware image (`air360.css`, `air360.js`, page body templates)

### Public headers

Headers under `main/include/air360/` define the public C++ interfaces used inside the component:

- `app.hpp`, `build_info.hpp`, `config_repository.hpp`, `network_manager.hpp`
- `cellular_config_repository.hpp`, `cellular_manager.hpp`, `connectivity_checker.hpp`, `modem_gpio.hpp`
- `status_service.hpp`, `time_utils.hpp`, `web_server.hpp`, `web_assets.hpp`, `web_ui.hpp`
- `sensors/` — sensor types, registry, transport, config, driver interface
- `uploads/` — backend config, measurement store, upload transport, uploader interfaces

### Sensor subsystem

Headers: `main/include/air360/sensors/`  
Sources: `main/src/sensors/`

Core files:

- `sensor_types.hpp` — enums for sensor types, transports, runtime states, and measurement value kinds
- `sensor_config.hpp` — persisted `SensorRecord` and `SensorConfigList` (up to 8 sensors)
- `sensor_driver.hpp` — `ISensorDriver` interface and generic measurement model
- `sensor_registry.hpp` / `sensor_registry.cpp` — static catalog of supported sensors with factory and validator per type
- `sensor_config_repository.cpp` — NVS-backed sensor config persistence and schema validation
- `sensor_manager.cpp` — orchestrator and `air360_sensor` FreeRTOS polling task (stack 6 KB, priority 5)
- `transport_binding.cpp` — `I2cBusManager` and `UartPortManager`: shared hardware bus lifecycle for drivers

Driver implementations under `main/src/sensors/drivers/`:

| File | Sensor | Backend |
|------|--------|---------|
| `bme280_sensor.cpp` | BME280 | `espressif__bme280` (managed component) |
| `bme680_sensor.cpp` | BME680 | `esp-idf-lib__bme680` (managed component) |
| `sps30_sensor.cpp` | SPS30 | `third_party/sps30` (vendored) |
| `scd30_sensor.cpp` | SCD30 | `esp-idf-lib__scd30` (managed component) |
| `veml7700_sensor.cpp` | VEML7700 | `esp-idf-lib__veml7700` (managed component) |
| `htu2x_sensor.cpp` | HTU2X / Si7021 | `esp-idf-lib__si7021` (managed component) |
| `sht4x_sensor.cpp` | SHT4X | `esp-idf-lib__sht4x` (managed component) |
| `gps_nmea_sensor.cpp` | GPS NMEA | `cinderblocks__esp_tinygpsplusplus` (managed component) |
| `dht_sensor.cpp` | DHT11 / DHT22 | `esp-idf-lib__dht` (managed component) |
| `ds18b20_sensor.cpp` | DS18B20 | `espressif__ds18b20` (managed component) |
| `me3_no2_sensor.cpp` | ME3-NO2 (ADC) | `esp_adc` (IDF built-in) |
| `sensirion_i2c_hal.cpp` | Sensirion I2C HAL | (bridge for SPS30 vendored lib) |

Adding a new sensor means one new driver file plus one registry entry — no changes to the rest of the runtime.

### Upload subsystem

Headers: `main/include/air360/uploads/`  
Sources: `main/src/uploads/`

- `measurement_store.cpp` — in-memory ring buffer (max 256 samples) with pending/inflight upload semantics
- `backend_config_repository.cpp` — NVS-backed `BackendConfigList` persistence (up to 4 backends; 3 built-in descriptors)
- `backend_registry.cpp` — static catalog of supported backends with factory and validator per type
- `upload_manager.cpp` — `air360_upload` FreeRTOS task (stack 7 KB, priority 4); upload cycle, backlog drain
- `upload_transport.cpp` — `esp_http_client` wrapper with CRT bundle support
- `adapters/air360_json_payload.cpp` — shared Air360 JSON body builder used by multiple backend uploaders
- `adapters/air360_api_uploader.cpp` — PUT to the configured Air360 backend URL (default `https://api.air360.ru/v1/devices/{chip_id}/batches/{batch_id}`)
- `adapters/custom_upload_uploader.cpp` — POST the Air360 JSON body to a user-supplied full HTTP(S) URL
- `adapters/sensor_community_uploader.cpp` — POST to the configured Sensor.Community URL (default `https://api.sensor.community/v1/push-sensor-data/`)

### Third-party sources

`main/third_party/sps30/` — vendored Sensirion SPS30 C library. All other sensor integrations are consumed as ESP-IDF managed components under `managed_components/`.

---

## HTTP surface

| Route | Description |
|-------|-------------|
| `GET /` | Runtime overview (HTML). Redirects to `/config` in setup AP mode. |
| `GET /diagnostics` | HTML: memory, task, network recovery, and raw status JSON dump |
| `GET /config` | Device, Wi-Fi, and cellular config form. Shows SSID scan dropdown in AP mode. |
| `POST /config` | Save device and cellular config |
| `GET /sensors` | Category-based sensor config page with staged apply/discard |
| `POST /sensors` | Stage or apply sensor config |
| `GET /backends` | Backend config form |
| `POST /backends` | Save backend config |
| `GET /wifi-scan` | JSON: cached SSID scan list (AP mode only) |
| `POST /check-sntp` | Test SNTP server reachability before saving |
| `GET /assets/*` | Embedded CSS and JS (`air360.css`, `air360.js`) |

---

## Build workflow

```bash
cd firmware
. "$HOME/.espressif/v6.0/esp-idf/export.sh"
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/tty.usbserial-0001 flash monitor
```

VS Code: open `firmware/` directly or `firmware/firmware.code-workspace` with the ESP-IDF extension.

---

## How to navigate as a contributor

- **Boot order** → `main/src/app.cpp`
- **Web routes and UI** → `main/src/web_server.cpp`, `status_service.cpp`, `web_ui.cpp`
- **Device persistence** → `main/src/config_repository.cpp`
- **Cellular modem** → `main/src/cellular_manager.cpp` (lifecycle), `cellular_config_repository.cpp` (persistence)
- **Sensor catalog** → `main/src/sensors/sensor_registry.cpp` (read before any driver)
- **Sensor config persistence** → `main/src/sensors/sensor_config_repository.cpp`
- **Measurement ownership and queueing** → `main/src/uploads/measurement_store.cpp`
- **Upload scheduling** → `main/src/uploads/upload_manager.cpp`
