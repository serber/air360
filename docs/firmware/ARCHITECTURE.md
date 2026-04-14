# Air360 Firmware — Architecture

## Purpose

Air360 is an air quality monitoring device built on the ESP32-S3. The firmware collects environmental measurements from attached sensors, exposes a local web interface for device configuration and status monitoring, and uploads batched measurement data to one or more remote backends.

The firmware runs continuously as an embedded application under FreeRTOS and ESP-IDF 6.x. It is written in C++17 and targets a 16 MB flash ESP32-S3 module.

---

## Firmware project layout

```
firmware/
├── CMakeLists.txt                  ESP-IDF project root (project name: air360_firmware)
├── sdkconfig.defaults              Repository-level build defaults
├── sdkconfig                       Full effective config for the current build
├── partitions.csv                  Custom flash partition table
├── firmware.code-workspace         VS Code workspace entry point
├── main/
│   ├── CMakeLists.txt              Component registration: sources, embeds, REQUIRES
│   ├── Kconfig.projbuild           CONFIG_AIR360_* options for menuconfig
│   ├── include/air360/             Public headers (one per module)
│   │   ├── sensors/                Sensor types, registry, transport, driver interfaces
│   │   └── uploads/                Backend config, transport, measurement store, uploaders
│   ├── src/                        Implementation files
│   │   ├── sensors/
│   │   │   ├── drivers/            13 concrete sensor driver implementations
│   │   │   ├── sensor_manager.cpp
│   │   │   ├── sensor_registry.cpp
│   │   │   ├── sensor_config_repository.cpp
│   │   │   └── transport_binding.cpp
│   │   └── uploads/
│   │       ├── adapters/           air360_api_uploader.cpp, sensor_community_uploader.cpp
│   │       ├── upload_manager.cpp
│   │       ├── upload_transport.cpp
│   │       ├── measurement_store.cpp
│   │       ├── backend_config_repository.cpp
│   │       └── backend_registry.cpp
│   ├── webui/                      Hand-authored HTML/CSS/JS assets embedded via EMBED_TXTFILES
│   └── third_party/sps30/          Vendored Sensirion SPS30 C library
└── managed_components/             ESP-IDF component manager dependencies
```

---

## Startup sequence

Boot is handled by `app_main.cpp` and `app.cpp`. `app_main()` constructs a static `air360::App` instance and calls `App::run()`.

`App::run()` performs a 9-step initialization sequence:

| Step | Action | Notes |
|------|--------|-------|
| 1 | Boot LED init | GPIO 11 = green, GPIO 10 = red |
| 2 | Watchdog arm | 10-second timeout, panic disabled |
| 3 | NVS flash init | Auto-erase on partition mismatch |
| 4 | Network core init | `esp_netif_init()`, default event loop |
| 5 | Device config load/create | NVS namespace `air360`, key `device_cfg` |
| 6 | Boot counter increment | NVS key `boot_count` (u32) |
| 7 | Sensor config load/create | NVS key `sensor_cfg` |
| 8 | Sensor manager start | Builds managed sensors, launches `air360_sensor` task |
| 9 | Network mode resolution | Station join → SNTP → fallback to Lab AP |

After the startup sequence, `App::run()` starts `UploadManager` and `WebServer`, then enters a 10-second maintenance loop that retries SNTP synchronization when station uplink is available.

Full startup order with dependencies:

```
NVS
 └─ ConfigRepository          (device_cfg, boot_count)
 └─ CellularConfigRepository  (cellular_cfg)
 └─ SensorConfigRepository    (sensor_cfg)
     └─ SensorManager         → builds drivers, starts air360_sensor task
         └─ I2cBusManager     (shared I2C buses)
         └─ UartPortManager   (shared UART ports)
 └─ BackendConfigRepository   (backend_cfg)
     └─ UploadManager         → starts air360_upload task
         └─ MeasurementStore  (pending/inflight queues)
         └─ UploadTransport   (HTTP client)
             └─ Air360ApiUploader
             └─ SensorCommunityUploader
NetworkManager                (station join or Lab AP)
 └─ SNTP synchronization      (pool.ntp.org or configured server)
CellularManager               (PPP modem; spawned only when enabled)
 └─ ConnectivityChecker       (ICMP ping after PPP up)
WebServer                     (esp_http_server on port 80)
StatusService                 (HTML/JSON rendering for web routes)
```

---

## Runtime model

After startup the firmware runs four concurrent execution contexts:

| Context | Name | Type | Stack | Priority |
|---------|------|------|-------|----------|
| Main app loop | `app_main` task | FreeRTOS task (IDF main) | 8192 B | default |
| Sensor polling | `air360_sensor` | FreeRTOS task | 6144 B | 5 |
| Upload scheduling | `air360_upload` | FreeRTOS task | 7168 B | 4 |
| Cellular modem | `air360_cellular` | FreeRTOS task | 8192 B | 5 |

The cellular task is spawned only when `CellularConfig.enabled = 1`. HTTP request handling runs on the internal `esp_http_server` thread pool.

Modules are not event-driven at the application layer. Inter-module communication uses direct method calls under mutex protection. The only FreeRTOS synchronization primitive used above the driver level is an event group inside `NetworkManager` for station connect/fail signalling.

---

## Components and responsibilities

### `App` — `app.cpp` / `app_main.cpp`

Top-level runtime controller. Owns the startup sequence, boot LEDs, watchdog, and the 10-second maintenance loop. Constructs all other modules as static locals. No persistent state of its own.

**Log tag:** `air360.app`

---

### `ConfigRepository` — `config_repository.cpp`

Manages the `DeviceConfig` NVS blob (schema version 4).

**`DeviceConfig` fields:**

| Field | Type | Notes |
|-------|------|-------|
| magic | `uint32_t` | `0x41333630` ("A360") |
| schema_version | `uint16_t` | Current: 4 |
| device_name | `char[32]` | Default: `air360` |
| http_port | `uint16_t` | Default: 80 |
| wifi_sta_ssid | `char[33]` | Wi-Fi station SSID |
| wifi_sta_password | `char[65]` | Wi-Fi station password |
| lab_ap_ssid | `char[33]` | Lab AP SSID |
| lab_ap_password | `char[65]` | Lab AP password |
| lab_ap_enabled | `uint8_t` | Default: 1 |
| local_auth_enabled | `uint8_t` | Reserved, not enforced |
| sntp_server | `char[64]` | Empty = use `pool.ntp.org` |
| sta_use_static_ip | `uint8_t` | 0=DHCP, 1=static |
| sta_ip | `char[16]` | Static IP address |
| sta_netmask | `char[16]` | Static subnet mask |
| sta_gateway | `char[16]` | Static gateway |
| sta_dns | `char[16]` | Static DNS; empty = use gateway |

On load: magic, schema version, or blob size mismatch triggers replacement with defaults (no migration).

**Log tag:** `air360.config`

---

### `CellularConfigRepository` — `cellular_config_repository.cpp`

Manages the `CellularConfig` NVS blob (schema version 1). Independent of `DeviceConfig` — versioned separately under the same `air360` NVS namespace.

**`CellularConfig` fields:**

| Field | Type | Default | Notes |
|-------|------|---------|-------|
| magic | `uint32_t` | `0x43454C4C` ("CELL") | |
| enabled | `uint8_t` | 0 | 1 = cellular uplink active |
| uart_port | `uint8_t` | 1 (UART1) | UART port for modem DTE |
| uart_rx_gpio | `uint8_t` | 18 | ESP32 RX (modem TX) |
| uart_tx_gpio | `uint8_t` | 17 | ESP32 TX (modem RX) |
| uart_baud | `uint32_t` | 115200 | |
| pwrkey_gpio | `uint8_t` | 12 | 0xFF = not wired |
| sleep_gpio | `uint8_t` | 21 | DTR/sleep; 0xFF = not wired |
| reset_gpio | `uint8_t` | 0xFF | Hardware reset; 0xFF = not wired |
| wifi_debug_window_s | `uint16_t` | 600 | Wi-Fi stays up N seconds alongside cellular |
| apn | `char[64]` | `""` | Required when enabled |
| username | `char[32]` | `""` | Optional PAP credential |
| password | `char[64]` | `""` | Optional PAP credential |
| sim_pin | `char[8]` | `""` | Optional SIM PIN |
| connectivity_check_host | `char[64]` | `"8.8.8.8"` | ICMP ping target; empty = skip |

**Log tag:** `air360.cellular_cfg`

---

### `NetworkManager` — `network_manager.cpp`

Manages Wi-Fi station join, Lab AP fallback, and SNTP synchronization.

**Network modes:**

| Mode | Description |
|------|-------------|
| `kOffline` | No active connection |
| `kSetupAp` | Lab AP at `192.168.4.1`, DHCP server on `192.168.4.0/24` |
| `kStation` | STA with DHCP-assigned IP |

**Station join flow:**
1. Create STA netif (once)
2. Register WIFI_EVENT and IP_EVENT handlers
3. Set hostname from `device_name` (alphanumeric, lowercased)
4. Wait for IP or failure with 15-second timeout (resets watchdog while waiting)
5. On success: call `synchronizeTime()`

**SNTP flow:**
- Server: `pool.ntp.org`
- Polls every 250 ms, resets watchdog while waiting
- Validates `unix_ms > 0`
- Timeout: 15 seconds

**Lab AP mode:**
- Static IP: `192.168.4.1 / 255.255.255.0`
- Optionally scans for available station networks
- Stores scan results for the `/wifi-scan` endpoint

**Synchronization:** Static FreeRTOS event group `station_events` with bits `kStationConnectedBit` (0) and `kStationFailedBit` (1).

**Log tag:** `air360.net`

---

### `CellularManager` — `cellular_manager.cpp`

Manages the SIM7600E modem lifecycle. Spawned only when `CellularConfig.enabled = 1`.

**`air360_cellular` FreeRTOS task:**
- Stack: 8192 bytes
- Priority: 5
- Runs an indefinite reconnect loop

**Connect sequence (`attemptConnect`):**
1. Allocate PPP netif
2. Configure DTE (UART, buffers)
3. Create SIM7600E DCE
4. Set APN (PDP context)
5. Unlock SIM PIN if configured
6. Poll signal quality until registered (up to 60 s, 2 s intervals)
7. Set PPP PAP auth if username/password configured
8. Enter PPP data mode
9. Wait for IP event; run connectivity check if host configured
10. Block until PPP session drops, then tear down

**Reconnect backoff:** exponential from 10 s, capped at 5 min. After 5 consecutive failures, performs PWRKEY hardware reset before next attempt.

**Wi-Fi debug window:** after PPP is up, the task schedules a call to `NetworkManager` to stop the Wi-Fi station after `wifi_debug_window_s` seconds.

**Runtime state (`CellularState`):**

| Field | Description |
|-------|-------------|
| `enabled` | Whether cellular is configured |
| `modem_detected` | DTE/DCE created successfully |
| `sim_ready` | SIM not locked |
| `registered` | Network registration complete |
| `ppp_connected` | Active PPP session |
| `ip_address` | Assigned PPP IP |
| `rssi_dbm` | Signal strength in dBm (from AT+CSQ) |
| `connectivity_ok` | Last ICMP ping result |
| `connectivity_check_skipped` | True when check host is empty |
| `last_error` | Last failure reason string |

**Log tag:** `air360.cellular`

---

### `SensorConfigRepository` — `sensors/sensor_config_repository.cpp`

Manages the `SensorConfigList` NVS blob (up to 8 sensors).

**Per-sensor config:**

| Field | Description |
|-------|-------------|
| id | Non-zero sensor identifier |
| enabled | Active flag |
| type | `SensorType` enum |
| transport | `SensorTransport` enum (inferred from type) |
| poll_interval_ms | 5000–3600000 ms |
| i2c.bus_id | Forced to 0 |
| i2c.address | I2C 7-bit address |
| uart.port_id | UART_NUM_1 or UART_NUM_2 |
| uart.rx_gpio / tx_gpio | Pin numbers |
| uart.baud_rate | 1200–115200 |
| gpio.gpio_pin | Must be in allowed board set |

**Log tag:** `air360.sensor_cfg`

---

### `SensorRegistry` — `sensors/sensor_registry.cpp`

Static catalog of all supported sensor types. Each entry (`SensorDescriptor`) holds:
- type enum value
- display name
- default transport
- default I2C address (for I2C sensors)
- default UART/GPIO values (for board-pin sensors)
- minimum poll interval
- `validateRecord()` polymorphic validator
- `createDriver()` factory function

**Registered sensor types:**

| Type | Transport | Default Address | Min Poll |
|------|-----------|----------------|----------|
| BME280 | I2C | 0x76 | 10 s |
| BME680 | I2C | 0x77 | 5 s |
| SPS30 | I2C | 0x69 | 5 s |
| SCD30 | I2C | 0x61 | 5 s |
| VEML7700 | I2C | 0x10 | 5 s |
| HTU2X | I2C | 0x40 | 5 s |
| SHT4X | I2C | 0x44 | 5 s |
| SDS011 | UART2 | — | 5 s |
| GPS (NMEA) | UART1 | — | 5 s |
| DHT11 | GPIO | — | 2 s |
| DHT22 | GPIO | — | 2 s |
| DS18B20 | GPIO (1-Wire) | — | 5 s |
| ME3-NO2 | Analog (ADC) | — | 5 s |

---

### `TransportBinding` — `sensors/transport_binding.cpp`

Manages shared hardware bus resources.

**`I2cBusManager`:**
- Up to 2 buses (`bus_id` 0–1)
- Clock: 100 kHz
- Transfer timeout: 200 ms
- Max 8 devices per bus
- Thread-safe via static mutex

**`UartPortManager`:**
- UART_NUM_1 and UART_NUM_2 only (UART_NUM_0 = console)
- RX buffer: 4096 bytes, TX buffer: 0
- Baud range: 1200–115200
- Detects and warns on console-pin overlap

**Log tag:** `air360.transport`

---

### `SensorManager` — `sensors/sensor_manager.cpp`

Owns the sensor runtime lifecycle.

**`air360_sensor` FreeRTOS task:**
- Stack: 6144 bytes
- Priority: 5
- Loop period: 250 ms
- Thread-safe sensors collection protected by static mutex

**Lifecycle methods:**
- `applyConfig(SensorConfigList)` — validates config, stops old task, instantiates drivers, starts task
- `buildManagedSensors()` — validates transport bindings, calls `SensorRegistry::createDriver()` for each enabled sensor
- `taskMain()` — polls each sensor at its configured interval, updates measurements, handles errors

**Sensor runtime states:**

| State | Meaning |
|-------|---------|
| `kDisabled` | Not in active config |
| `kConfigured` | Config loaded, driver not yet initialized |
| `kInitialized` | Driver init succeeded |
| `kPolling` | Actively polling |
| `kAbsent` | Poll timed out (device not found) |
| `kUnsupported` | No driver available |
| `kError` | Init or poll failed, 5-second retry backoff |

---

### Sensor drivers — `sensors/drivers/`

Each driver wraps an ESP-IDF managed component or vendored library and implements a common `ISensorDriver` interface. Drivers are stateless between `init()` / `read()` calls and are owned exclusively by `SensorManager`.

| Driver file | Sensor | Managed component |
|-------------|--------|-------------------|
| `bme280_sensor.cpp` | BME280 | `espressif__bme280` |
| `bme680_sensor.cpp` | BME680 | `esp-idf-lib__bme680` |
| `sps30_sensor.cpp` | SPS30 | `third_party/sps30` (vendored) |
| `scd30_sensor.cpp` | SCD30 | `esp-idf-lib__scd30` |
| `veml7700_sensor.cpp` | VEML7700 | `esp-idf-lib__veml7700` |
| `htu2x_sensor.cpp` | HTU2X (Si7021) | `esp-idf-lib__si7021` |
| `sht4x_sensor.cpp` | SHT4X | `esp-idf-lib__sht4x` |
| `gps_nmea_sensor.cpp` | GPS NMEA | `cinderblocks__esp_tinygpsplusplus` |
| `dht_sensor.cpp` | DHT11 / DHT22 | `esp-idf-lib__dht` |
| `ds18b20_sensor.cpp` | DS18B20 | `espressif__ds18b20` |
| `me3_no2_sensor.cpp` | ME3-NO2 (analog) | `esp_adc` (IDF built-in) |
| `sensirion_i2c_hal.cpp` | Sensirion HAL | (I2C HAL for SPS30) |

GPS reports: latitude, longitude, altitude, satellites, speed, course, HDOP — through the generic `measurements` value array.

---

### `MeasurementStore` — `uploads/measurement_store.cpp`

In-memory bounded ring buffer for measurement samples. All access is protected by a static mutex.

**Limits:**
- Max queued samples: 256
- Oldest sample dropped on overflow (ring buffer semantics)

**Lifecycle:**
1. `SensorManager` calls `recordMeasurement()` after each successful poll — latest reading per sensor is tracked separately from the queue
2. `UploadManager` calls `beginUploadWindow(N)` to atomically move up to N pending samples to `inflight`
3. On upload success: `acknowledgeInflight()` clears the inflight set
4. On upload failure: `restoreInflight()` moves inflight samples back to pending

**`MeasurementBatch` structure (passed to uploaders):**

```cpp
struct MeasurementBatch {
    uint64_t  batch_id;
    int64_t   created_unix_ms;
    string    device_name, board_name;
    string    chip_id, short_chip_id, esp_mac_id;
    string    project_version;
    NetworkMode  network_mode;
    bool      station_connected;
    vector<MeasurementPoint> points;  // {sensor_id, type, value_kind, value, sample_time_ms}
};
```

---

### `BackendConfigRepository` — `uploads/backend_config_repository.cpp`

Manages the `BackendConfigList` NVS blob (up to 2 backends).

**Per-backend config:**

| Field | Description |
|-------|-------------|
| type | `BackendType` enum |
| enabled | Active flag |
| display_name | `char[32]` |
| endpoint_url | `char[256]` (static defaults) |
| device_id_override | `char[64]` |
| bearer_token | `char[256]` (reserved) |
| upload_interval_s | 10–300 seconds (default 145 s) |

**Log tag:** `air360.backend_cfg`

---

### `BackendRegistry` — `uploads/backend_registry.cpp`

Static catalog of supported backend types. Each entry (`BackendDescriptor`) holds:
- type enum value
- display name
- default endpoint URL
- `validateRecord()` validator
- `createUploader()` factory function

**Registered backends:**

| Type | Default endpoint |
|------|-----------------|
| Sensor.Community | `http://api.sensor.community/v1/push-sensor-data/` |
| Air360 API | `http://api.air360.ru` |

---

### `UploadManager` — `uploads/upload_manager.cpp`

Manages the upload cycle and per-backend runtime state.

**`air360_upload` FreeRTOS task:**
- Stack: 7168 bytes
- Priority: 4
- Normal loop delay: 1000 ms
- Upload interval: 145 s (configurable per backend, 10–300 s)
- Backlog drain interval: 5 s (applied when upload succeeded and pending > 0)

**Upload preconditions (checked every cycle):**
- Network uplink active (Wi-Fi station connected **or** cellular PPP connected)
- Valid SNTP unix time (`unix_ms > 0`)

**Upload cycle:**
1. Check preconditions
2. Call `MeasurementStore::beginUploadWindow(32)` — up to 32 samples per batch
3. Assemble `MeasurementBatch`
4. Call each enabled backend adapter
5. `acknowledgeInflight()` on success, `restoreInflight()` on failure
6. Update per-backend runtime state

**Backend runtime states:**

| State | Meaning |
|-------|---------|
| `kDisabled` | Not enabled |
| `kIdle` | Waiting for next upload cycle |
| `kUploading` | Upload in progress |
| `kOk` | Last upload succeeded |
| `kError` | Last upload failed |
| `kNotImplemented` | No adapter registered |

**Upload result codes:** `kSuccess`, `kHttpError`, `kTransportError`, `kConfigError`, `kNoNetwork`, `kNoData`, `kUnsupported`, `kUnknown`

**Log tag:** `air360.upload`

---

### `UploadTransport` — `uploads/upload_transport.cpp`

Low-level HTTP client wrapper around `esp_http_client`.

**Configuration:**
- CRT bundle enabled (TLS support)
- Per-request timeout: 15 000 ms
- Request/response buffer: 512 bytes
- Keep-alive: disabled
- Methods: POST, PUT

Returns per-request: transport status, HTTP status code, response body size, total duration.

**Log tag:** `air360.http`

---

### `Air360ApiUploader` — `uploads/adapters/air360_api_uploader.cpp`

Uploads batches to the Air360 backend.

- Method: `PUT`
- Path: `/v1/devices/{chip_id}/batches/{batch_id}`
- Content-Type: `application/json`
- Auth: none (bearer token reserved but not sent)
- Success HTTP codes: 200–208, 409

Payload is the full `MeasurementBatch` serialized as JSON with `schema_version`.

---

### `SensorCommunityUploader` — `uploads/adapters/sensor_community_uploader.cpp`

Uploads to [Sensor.Community](https://sensor.community/).

- Method: `POST`
- Endpoint: `http://api.sensor.community/v1/push-sensor-data/`
- Headers: `X-Sensor`, `X-MAC-ID`, `X-PIN`, `User-Agent`
- Format: `{"sensordatavalues": [{"value_type": "...", "value": "..."}]}`

Supported sensor types: BME280, BME680, DHT11/22, DS18B20, GPS, SPS30.

`X-Sensor` uses the legacy `esp32-{chip_id}` format (or the configured device ID override).

---

### `WebServer` — `web_server.cpp`

HTTP server running on port 80 (configurable via `CONFIG_AIR360_HTTP_PORT`).

**esp_http_server configuration:**
- Stack size: 10 KB
- Max URI handlers: 12

**Registered routes:**

| Route | Description |
|-------|-------------|
| `GET /` | Overview page (HTML) |
| `GET /diagnostics` | Diagnostics page (HTML with raw status JSON dump) |
| `GET /config` | Device config page (HTML) |
| `POST /config` | Save device config |
| `GET /sensors` | Sensor management page (HTML) |
| `POST /sensors` | Stage or apply sensor config |
| `GET /backends` | Backend management page (HTML) |
| `POST /backends` | Save backend config |
| `GET /wifi-scan` | JSON list of scanned SSIDs |
| `GET /assets/*` | Embedded CSS/JS assets |

In **setup AP mode**: `/`, `/sensors`, and `/backends` redirect to `/config`. Navigation is restricted to the device section.

Sensor edits are staged in memory until the user explicitly clicks **Apply now**, which persists the staged list and rebuilds the sensor runtime without rebooting. Backend changes are persisted immediately.

**Log tag:** `air360.web`

---

### `StatusService` — `status_service.cpp`

Produces HTML and JSON payloads for web routes. Aggregates runtime state from all modules:
- Build info and device identity
- Network mode, IP address, SNTP state
- Sensor list with states, last measurements, and queued sample counts
- `MeasurementStore` pending/inflight counts
- Backend statuses with last result, duration, retry count
- Health checks (time sync, sensor freshness, uplink, backend health)

The raw status JSON rendered inside the Diagnostics page includes `health_status`, `health_summary`, and `health_checks`.

---

### `BuildInfo` — `build_info.cpp`

Reads project metadata at runtime from ESP-IDF ROM/flash info.

**Device identity fields:**

| Field | Description |
|-------|-------------|
| `chip_id` | 48-bit decimal (6 MAC bytes) |
| `short_chip_id` | Legacy airrohr format (24-bit, lower 3 bytes) |
| `esp_mac_id` | Station MAC in hex (12 chars) |

Detects chip family (ESP32-S3, ESP32-C3, etc.), features (Wi-Fi, BLE, PSRAM), core count, crystal frequency, and package variant.

---

## Configuration model

### Compile-time (`Kconfig.projbuild` / `sdkconfig.defaults`)

| Option | Default | Meaning |
|--------|---------|---------|
| `CONFIG_AIR360_BOARD_NAME` | `esp32-s3-devkitc-1` | Board label in status |
| `CONFIG_AIR360_DEVICE_NAME` | `air360` | Default device name |
| `CONFIG_AIR360_HTTP_PORT` | 80 | Web server port |
| `CONFIG_AIR360_I2C0_SDA_GPIO` | 8 | I2C bus 0 SDA |
| `CONFIG_AIR360_I2C0_SCL_GPIO` | 9 | I2C bus 0 SCL |
| `CONFIG_AIR360_GPS_DEFAULT_UART_PORT` | UART1 | GPS UART port |
| `CONFIG_AIR360_GPS_DEFAULT_RX_GPIO` | 18 | GPS RX pin |
| `CONFIG_AIR360_GPS_DEFAULT_TX_GPIO` | 17 | GPS TX pin |
| `CONFIG_AIR360_GPS_DEFAULT_BAUD_RATE` | 9600 | GPS baud |
| `CONFIG_AIR360_SDS011_DEFAULT_UART_PORT` | UART2 | SDS011 UART port |
| `CONFIG_AIR360_SDS011_DEFAULT_RX_GPIO` | 38 | SDS011 RX pin |
| `CONFIG_AIR360_SDS011_DEFAULT_TX_GPIO` | 39 | SDS011 TX pin |
| `CONFIG_AIR360_SDS011_DEFAULT_BAUD_RATE` | 9600 | SDS011 baud |
| `CONFIG_AIR360_GPIO_SENSOR_PIN_0` | GPIO4 | Slot 0 pin |
| `CONFIG_AIR360_GPIO_SENSOR_PIN_1` | GPIO5 | Slot 1 pin |
| `CONFIG_AIR360_GPIO_SENSOR_PIN_2` | GPIO6 | Slot 2 pin |
| `CONFIG_AIR360_ENABLE_LAB_AP` | y | Lab AP default on |
| `CONFIG_AIR360_LAB_AP_SSID` | `air360` | Lab AP SSID |
| `CONFIG_AIR360_LAB_AP_PASSWORD` | `air360password` | Lab AP password |
| `CONFIG_AIR360_LAB_AP_CHANNEL` | 1 | AP Wi-Fi channel |
| `CONFIG_AIR360_LAB_AP_MAX_CONNECTIONS` | 4 | AP max stations |
| `CONFIG_AIR360_CELLULAR_DEFAULT_UART` | 1 | Modem UART port |
| `CONFIG_AIR360_CELLULAR_DEFAULT_RX_GPIO` | 18 | Modem RX GPIO |
| `CONFIG_AIR360_CELLULAR_DEFAULT_TX_GPIO` | 17 | Modem TX GPIO |
| `CONFIG_AIR360_CELLULAR_DEFAULT_PWRKEY_GPIO` | 12 | Modem PWRKEY GPIO |
| `CONFIG_AIR360_CELLULAR_DEFAULT_SLEEP_GPIO` | 21 | Modem SLEEP/DTR GPIO |
| `CONFIG_AIR360_CELLULAR_WIFI_DEBUG_WINDOW_S` | 600 | Wi-Fi stays up N s after PPP connects |

AP channel and max connections remain compile-time constants. All other options become defaults in the first persisted `DeviceConfig`.

### Runtime (NVS)

Four independent NVS blobs under namespace `air360`:

| Key | Structure | Description |
|-----|-----------|-------------|
| `device_cfg` | `DeviceConfig` | Device name, Wi-Fi creds, SNTP, static IP, HTTP port |
| `cellular_cfg` | `CellularConfig` | Modem UART/GPIO, carrier APN, credentials |
| `sensor_cfg` | `SensorConfigList` (up to 8 entries) | Sensor inventory |
| `backend_cfg` | `BackendConfigList` (up to 2 entries) | Backend targets and upload interval |
| `boot_count` | `uint32_t` | Incremented on every boot |

Schema version and magic number guard each blob. Mismatch triggers replacement with defaults (no migration).

---

## Storage and partitions

Partition table defined in `partitions.csv`:

| Name | Type | Offset | Size | Runtime usage |
|------|------|--------|------|---------------|
| `nvs` | data/nvs | 0x9000 | 24 KB | DeviceConfig, SensorConfig, BackendConfig, boot counter |
| `otadata` | data/ota | 0xf000 | 8 KB | OTA metadata (present, OTA logic not yet implemented) |
| `phy_init` | data/phy | 0x11000 | 4 KB | PHY calibration (managed by IDF) |
| `factory` | app/factory | 0x20000 | 1536 KB | Application image |
| `storage` | data/spiffs | 0x1a0000 | 384 KB | Reserved, not mounted or used |

The current runtime depends only on NVS. SPIFFS and OTA partitions are reserved for future use.

---

## Hardware integration

### GPIO allocation

| GPIO | Role | Configurable |
|------|------|-------------|
| 4 | GPIO sensor slot 0 | Kconfig |
| 5 | GPIO sensor slot 1 | Kconfig |
| 6 | GPIO sensor slot 2 | Kconfig |
| 8 | I2C bus 0 SDA | Kconfig |
| 9 | I2C bus 0 SCL | Kconfig |
| 10 | Red boot LED | No |
| 11 | Green boot LED | No |
| 12 | Modem PWRKEY (default) | Kconfig / CellularConfig |
| 17 | GPS TX / Modem TX (shared default) | Kconfig |
| 18 | GPS RX / Modem RX (shared default) | Kconfig |
| 21 | Modem SLEEP/DTR (default) | Kconfig / CellularConfig |
| 38 | SDS011 RX (default) | Kconfig |
| 39 | SDS011 TX (default) | Kconfig |

> **GPIO17/18 conflict:** GPS (NMEA) and the SIM7600E modem share the same default UART1 pins. They cannot be used simultaneously. If both are needed, reconfigure one via Kconfig before building.

### I2C

- Bus 0: SDA=GPIO8, SCL=GPIO9
- Clock: 100 kHz
- Transfer timeout: 200 ms
- Used by: BME280, BME680, SPS30, SCD30, VEML7700, HTU2X, SHT4X

### UART

| Port | Default assignment | Baud | RX buffer |
|------|--------------------|------|-----------|
| UART0 | Console (reserved) | — | — |
| UART1 | GPS (RX=GPIO18, TX=GPIO17) **or** SIM7600E modem | 9600 / 115200 | 4096 B |
| UART2 | SDS011 (RX=GPIO38, TX=GPIO39) | 9600 | 4096 B |

The modem DTE uses 4096 B RX / 512 B TX ring buffers by default.

### GPIO / Analog

- GPIO4, GPIO5, GPIO6: shared slots for DHT11, DHT22, DS18B20 (1-Wire), ME3-NO2 (ADC)
- One sensor per slot; selection through the `/sensors` web UI

---

## External interfaces

### Local HTTP server

Runs on port 80. Serves a server-rendered HTML UI with embedded CSS/JS assets. Page body templates are stored in `main/webui/` and embedded into the firmware image via `EMBED_TXTFILES`. `web_ui.cpp` provides template expansion, page shell, navigation, and HTML escaping so UI changes do not require editing large inline strings in handler code.

### Air360 backend API

- Base: `http://api.air360.ru`
- Protocol: HTTP (HTTPS deferred due to unresolved connection-latency issues on ESP32-S3)
- Route: `PUT /v1/devices/{chip_id}/batches/{batch_id}`
- Payload: JSON `MeasurementBatch`

### Sensor.Community API

- Endpoint: `http://api.sensor.community/v1/push-sensor-data/`
- Protocol: HTTP
- Payload: `sensordatavalues` array
- Identification: `X-Sensor: esp32-{chip_id}`, `X-MAC-ID`, `X-PIN`

### SNTP

- Server: `pool.ntp.org`
- Required before any upload is attempted

---

## Logging and error handling

### Log tags

| Tag | Module |
|-----|--------|
| `air360.app` | Boot sequence |
| `air360.config` | ConfigRepository |
| `air360.net` | NetworkManager, SNTP |
| `air360.sensor` | SensorManager, polling |
| `air360.sensor_cfg` | SensorConfigRepository |
| `air360.transport` | I2cBusManager, UartPortManager |
| `air360.upload` | UploadManager |
| `air360.http` | UploadTransport |
| `air360.backend_cfg` | BackendConfigRepository |
| `air360.web` | WebServer |

### Error handling patterns

**NVS config:**
- Magic or schema version mismatch → replace with defaults, log WARNING
- Blob size mismatch → replace with defaults, log WARNING
- NVS init failure → `ESP_ERROR_CHECK` (fatal panic)

**Network:**
- Station join timeout → switch to Lab AP mode
- SNTP timeout → continue, retry in maintenance loop
- Station disconnect → mark `kOffline`, store reason code

**Sensor:**
- Driver init failure → mark `kError`, `driver_ready = false`
- Poll timeout → mark `kAbsent`
- Poll error → mark `kError`, apply 5-second retry backoff

**Upload:**
- No station or no unix time → `kNoNetwork`, skip, retry next cycle
- HTTP 4xx/5xx → `kHttpError`, log status code, increment retry counter
- Transport failure → `kTransportError`, log ESP error name
- Config error → `kConfigError`, skip backend

---

## Key constants and limits

| Constant | Value | Location |
|----------|-------|----------|
| Max configured sensors | 8 | `sensor_types.hpp` |
| Max configured backends | 2 | `backend_config.hpp` |
| Max measurement values per point | 16 | `sensor_types.hpp` |
| Max queued samples | 256 | `measurement_store.cpp` |
| Max samples per upload window | 32 | `upload_manager.cpp` |
| Watchdog timeout | 10 s | `app.cpp` |
| I2C clock | 100 kHz | `transport_binding.cpp` |
| I2C transfer timeout | 200 ms | `transport_binding.cpp` |
| UART RX buffer | 4096 B | `transport_binding.cpp` |
| HTTP request timeout | 15 000 ms | upload adapters |
| HTTP buffer size | 512 B | `upload_transport.cpp` |
| Upload interval default | 145 s | `backend_config.hpp` |
| Upload interval range | 10–300 s | `backend_config_repository.cpp` |
| Sensor poll interval range | 5 000–3 600 000 ms | `sensor_registry.cpp` |
| Upload backlog drain interval | 5 s | `upload_manager.cpp` |
| SNTP poll period | 250 ms | `network_manager.cpp` |
| Maintenance loop period | 10 s | `app.cpp` |
| Sensor task loop period | 250 ms | `sensor_manager.cpp` |

---

## Synchronization primitives

| Module | Primitive | Protected resource |
|--------|-----------|-------------------|
| `SensorManager` | Static mutex | `sensors_`, task handle |
| `MeasurementStore` | Static mutex | `pending_`, `inflight_`, `latest_by_sensor_` |
| `UploadManager` | Static mutex | `backends_`, runtime state |
| `I2cBusManager` | Static mutex | bus handles, device list |
| `NetworkManager` | Static event group | station connected / failed bits |

No application-level queues. `MeasurementStore` uses vector swap under mutex for atomic pending→inflight transition.

---

## Implemented vs planned

### Confirmed in implementation

- Full 9-step boot sequence
- NVS-backed config for device, sensors, and backends
- FreeRTOS sensor polling task with per-sensor scheduling
- 12 sensor driver types across I2C, UART, GPIO, and ADC transports
- In-memory measurement queue with pending/inflight upload semantics
- FreeRTOS upload task with backlog drain
- Air360 API and Sensor.Community adapters
- Server-rendered web UI with 5 HTML pages and embedded CSS/JS
- Lab AP mode at `192.168.4.1` with `/wifi-scan` endpoint
- SNTP synchronization gating upload start
- Health check aggregation in the Diagnostics raw JSON dump

### Planned or reserved but not yet implemented

- OTA firmware update logic (partition reserved)
- SPIFFS data partition (partition reserved, not mounted)
- Captive portal / DNS redirect in Lab AP mode
- Local auth enforcement (flag stored but not checked)
- Device config changes without reboot
- HTTPS for Air360 API (deferred, latency issues under investigation)
- Full API-driven UI (currently server-rendered and form-driven)
