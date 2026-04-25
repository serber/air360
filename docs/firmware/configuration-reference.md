# Configuration Reference

## Status

Implemented. Keep this reference aligned with the current persisted config models, defaults, and save-time validation rules.

## Scope

This is the field-level reference for editable firmware configuration across device, sensor, backend, and cellular domains.

## Source of truth in code

- `firmware/main/src/config_repository.cpp`
- `firmware/main/src/config_transaction.cpp`
- `firmware/main/src/sensors/sensor_config_repository.cpp`
- `firmware/main/src/uploads/backend_config_repository.cpp`
- `firmware/main/src/cellular_config_repository.cpp`
- `firmware/main/src/web_server.cpp`
- `firmware/main/include/air360/tuning.hpp`

## Read next

- [nvs.md](nvs.md)
- [web-ui.md](web-ui.md)
- [network-manager.md](network-manager.md)

The firmware stores all user-editable configuration in four NVS blobs. This document is a field-by-field reference for each configuration domain — defaults, valid ranges, and validation rules enforced at save time.

For storage format details (magic numbers, schema versions, struct layouts) see [nvs.md](nvs.md).

---

## Configuration domains

| Domain | NVS key | Struct | What it controls |
|--------|---------|--------|-----------------|
| Device | `device_cfg` | `DeviceConfig` | Network credentials, device identity, static IP, web server port |
| Cellular | `cellular_cfg` | `CellularConfig` | SIM7600E modem settings, carrier provisioning, cellular uplink |
| Sensors | `sensor_cfg` | `SensorConfigList` | Which sensors are active and how each is polled |
| Backends | `backend_cfg` | `BackendConfigList` | Upload destinations and upload interval |

All four are loaded at boot step 4–6, validated, and replaced with compiled-in defaults on any integrity failure. There is no migration — a schema change wipes stored values. The status JSON reports each repository load path under `config.<repository>.load_source` with per-source counters and `wrote_defaults` so operators can distinguish preserved NVS config from regenerated defaults.

---

## How to change configuration

Configuration is changed through the embedded web UI served on port 80, or via the HTTP API used by the web UI (see [web-ui.md](web-ui.md)). The three pages map to persisted domains as follows:

| Web UI page | Domain |
|-------------|--------|
| Device | `device_cfg` and `cellular_cfg` |
| Sensors | `sensor_cfg` |
| Backends | `backend_cfg` |

Changes are written to NVS immediately on save. The Device page stages `device_cfg` and `cellular_cfg` together and commits both with one NVS commit, so runtime copies are updated only after both records persist successfully. The device must be rebooted for most changes to take effect (network credentials, sensor list). Backend and upload interval changes are applied by the upload manager without a reboot.

---

## Compile-time defaults (`sdkconfig.defaults`)

Some fields are initialised from Kconfig constants baked into the firmware image. These are the values written when NVS has no stored config:

| Kconfig key | `sdkconfig.defaults` value | Field |
|-------------|---------------------------|-------|
| `CONFIG_AIR360_DEVICE_NAME` | `"air360"` | `device_name` |
| `CONFIG_AIR360_HTTP_PORT` | `80` | `http_port` |
| `CONFIG_AIR360_LAB_AP_SSID` | `"air360"` | `lab_ap_ssid` |
| `CONFIG_AIR360_LAB_AP_PASSWORD` | `"air360password"` | `lab_ap_password` |
| `CONFIG_AIR360_LAB_AP_CHANNEL` | `1` | AP channel (not stored in NVS) |
| `CONFIG_AIR360_LAB_AP_MAX_CONNECTIONS` | `4` | AP max clients (not stored in NVS) |
| `CONFIG_AIR360_I2C0_SDA_GPIO` | `8` | I2C SDA (not stored in NVS) |
| `CONFIG_AIR360_I2C0_SCL_GPIO` | `9` | I2C SCL (not stored in NVS) |
| `CONFIG_AIR360_GPS_DEFAULT_RX_GPIO` | `18` | GPS RX (stored in sensor record) |
| `CONFIG_AIR360_GPS_DEFAULT_TX_GPIO` | `17` | GPS TX (stored in sensor record) |
| `CONFIG_AIR360_GPS_DEFAULT_UART_PORT` | `1` | GPS UART port (stored in sensor record) |
| `CONFIG_AIR360_GPS_DEFAULT_BAUD_RATE` | `9600` | GPS baud rate (stored in sensor record) |
| `CONFIG_AIR360_WIFI_RECONNECT_BASE_DELAY_MS` | `10000` | First reconnect backoff after an unexpected Wi-Fi station drop |
| `CONFIG_AIR360_WIFI_RECONNECT_MAX_DELAY_MS` | `300000` | Upper cap for reconnect backoff while station recovery is active |
| `CONFIG_AIR360_WIFI_SETUP_AP_RETRY_DELAY_MS` | `180000` | Delay between background station retries while setup AP stays active |
| `CONFIG_AIR360_WIFI_DISCONNECT_IGNORE_WINDOW_MS` | `2000` | Ignore window for self-induced disconnect events during Wi-Fi mode changes |
| `CONFIG_AIR360_WIFI_CONNECT_TIMEOUT_MS` | `15000` | Timeout for synchronous station join and `ensureStationTime()`-driven joins |
| `CONFIG_AIR360_MEASUREMENT_QUEUE_DEPTH` | `256` | Shared queued-sample capacity before oldest uploads are dropped |
| `CONFIG_AIR360_BLE_PAYLOAD_REFRESH_INTERVAL_MS` | `5000` | Period between BTHome payload rebuilds in `air360_ble` |
| `CONFIG_AIR360_GPIO_SENSOR_PIN_0/1/2` | `4` / `5` / `6` | Valid GPIO slots for GPIO/analog sensors |

AP channel and max-connections are read from Kconfig at runtime and are **not** written to NVS.

### Runtime tuning Kconfig values

The values below are not persisted in NVS. They are build-time tuning knobs grouped in [`firmware/main/include/air360/tuning.hpp`](../../firmware/main/include/air360/tuning.hpp) and consumed directly by runtime subsystems.

| Kconfig key | Default | Purpose | Safe range / tradeoff |
|-------------|---------|---------|------------------------|
| `CONFIG_AIR360_WIFI_RECONNECT_BASE_DELAY_MS` | `10000` ms | First delay in the capped exponential reconnect backoff after an unexpected station disconnect | `1000`–`600000` ms. Lower values recover faster but can hammer an AP that is still rebooting; higher values make brief outages visible to users longer. |
| `CONFIG_AIR360_WIFI_RECONNECT_MAX_DELAY_MS` | `300000` ms | Upper cap for the reconnect backoff sequence | `10000`–`3600000` ms. Keep it above the base delay; higher values reduce retry pressure during long outages but slow recovery once the network returns. |
| `CONFIG_AIR360_WIFI_SETUP_AP_RETRY_DELAY_MS` | `180000` ms | Retry cadence for background station reconnect attempts while setup AP remains available | `10000`–`3600000` ms. Shorter intervals re-test Wi-Fi more aggressively; longer intervals reduce churn but keep the device in AP fallback longer. |
| `CONFIG_AIR360_WIFI_DISCONNECT_IGNORE_WINDOW_MS` | `2000` ms | Window that suppresses reconnect logic after intentional `esp_wifi_stop()` / `esp_wifi_disconnect()` calls | `100`–`60000` ms. Too short can re-arm reconnect on deliberate mode changes; too long can hide a real disconnect right after reconfiguration. |
| `CONFIG_AIR360_WIFI_CONNECT_TIMEOUT_MS` | `15000` ms | Timeout for blocking station join attempts and follow-up station recovery work | `5000`–`120000` ms. Must cover WPA join plus DHCP on slow APs; very large values delay setup-AP fallback and worker responsiveness. |
| `CONFIG_AIR360_MEASUREMENT_QUEUE_DEPTH` | `256` samples | Shared in-RAM backlog for uploadable measurements | `32`–`2048` samples. Higher values buy more outage tolerance at the cost of more RAM retained in `MeasurementStore`; lower values overflow sooner during WAN loss. |
| `CONFIG_AIR360_BLE_PAYLOAD_REFRESH_INTERVAL_MS` | `5000` ms | Period between BTHome payload rebuilds in the BLE advertiser task | `1000`–`60000` ms. Lower values show fresher readings over BLE but increase task wakeups and payload churn; higher values reduce activity but make BLE telemetry staler. |

---

## Device configuration (`device_cfg`)

Struct: `DeviceConfig`

### Fields

| Field | Type | Default | Constraints |
|-------|------|---------|-------------|
| `http_port` | `uint16_t` | `80` | 1–65535; must be non-zero |
| `lab_ap_enabled` | `uint8_t` | `1` | 0 or 1 |
| `local_auth_enabled` | `uint8_t` | `0` | Stored but not enforced |
| `wifi_power_save_enabled` | `uint8_t` | `0` | 0 = power save off; 1 = `WIFI_PS_MIN_MODEM` |
| `ble_advertise_enabled` | `uint8_t` | `0` | 0 = BLE off; 1 = BTHome v2 passive advertising |
| `ble_adv_interval_index` | `uint8_t` | `2` | Index into `{100, 300, 1000, 3000}` ms table; default 1000 ms |
| `device_name` | `char[32]` | `"air360"` | 1–31 chars, non-empty, null-terminated |
| `wifi_sta_ssid` | `char[33]` | `""` | 0–32 chars; empty = no station config |
| `wifi_sta_password` | `char[65]` | `""` | 0–63 chars |
| `lab_ap_ssid` | `char[33]` | `"air360"` | 1–32 chars, non-empty |
| `lab_ap_password` | `char[65]` | `"air360password"` | 0 chars (open) or 8–63 chars |
| `sntp_server` | `char[64]` | `""` | 0–63 printable ASCII chars; empty = use default (`pool.ntp.org`) |
| `sta_use_static_ip` | `uint8_t` | `0` | 0 = DHCP; 1 = static IP |
| `sta_ip` | `char[16]` | `""` | IPv4 dotted-decimal; max 15 chars; required when static IP enabled |
| `sta_netmask` | `char[16]` | `""` | IPv4 dotted-decimal; max 15 chars |
| `sta_gateway` | `char[16]` | `""` | IPv4 dotted-decimal; max 15 chars |
| `sta_dns` | `char[16]` | `""` | IPv4 dotted-decimal; max 15 chars; empty = use gateway |

### Validation rules (`ConfigRepository::isValid`)

- `http_port` must not be 0.
- `device_name` must not be empty.
- All string fields must be null-terminated within their buffer.
- `wifi_sta_ssid` length ≤ 32 (empty is valid — triggers setup AP mode at boot).
- `wifi_sta_password` length ≤ 63.
- `lab_ap_ssid` must be 1–32 characters.
- `lab_ap_password`: either empty (open AP) or 8–63 characters (WPA2-PSK).
- `sntp_server`: null-terminated within its buffer; 0–63 printable ASCII chars (no spaces or control characters); empty is valid and means "use firmware default".

### Notes

- `device_name` is used as the DHCP hostname: lowercased, non-alphanumeric characters replaced with `-`, trailing hyphens stripped. Falls back to `"air360"` if the result is empty.
- `lab_ap_enabled` is stored but the current firmware always starts the AP when station connection fails, regardless of this field.
- `local_auth_enabled` is reserved; the web server does not enforce it.
- `sntp_server`: when empty, `NetworkManager` uses `kDefaultSntpServer` (`pool.ntp.org`). When non-empty, the stored value is used as the NTP hostname on the next boot. The value is validated for printable ASCII before save; DNS resolution and reachability are tested via `POST /check-sntp` before committing.
- `sta_use_static_ip`: when `1`, `NetworkManager` applies the stored address/netmask/gateway/DNS to the `WIFI_STA_DEF` netif instead of using DHCP. Applies to station mode only; the setup AP is unaffected. When the config page is loaded and `sta_ip` is empty, the form pre-fills these fields from the current DHCP lease to make conversion easier.
- `wifi_power_save_enabled`: when `1`, `NetworkManager` calls `esp_wifi_set_ps(WIFI_PS_MIN_MODEM)` after Wi-Fi start instead of `WIFI_PS_NONE`. Reduces idle power consumption (~80–100 mA → ~20–30 mA average) at the cost of slightly increased upload latency. Applies to station mode only; the setup AP is unaffected.
- `ble_advertise_enabled`: when `1`, `BleAdvertiser` starts broadcasting sensor readings in BTHome v2 format on boot. The advertisement is non-connectable and requires no pairing. Home Assistant detects these packets automatically via its Bluetooth integration. Works independently of Wi-Fi state.
- `ble_adv_interval_index`: selects the BLE advertising interval from `{100, 300, 1000, 3000}` ms. Index 2 (1000 ms) is the default and is recommended for most Home Assistant setups. Shorter intervals increase radio activity and power draw; 100 ms is generally unnecessary for sensor telemetry.

---

## Cellular configuration (`cellular_cfg`)

Struct: `CellularConfig`  
NVS key: `cellular_cfg` (namespace `air360`) — loaded independently of `device_cfg`; saved together with `device_cfg` by the Device Configuration page.  
Schema version: 1.

### Fields

| Field | Type | Default | Notes |
|-------|------|---------|-------|
| `enabled` | `uint8_t` | `0` | `0` = disabled; `1` = cellular uplink active |
| `uart_port` | `uint8_t` | `1` (UART1) | UART port number for modem DTE |
| `uart_rx_gpio` | `uint8_t` | `18` | ESP32 RX pin (receives from modem TX) |
| `uart_tx_gpio` | `uint8_t` | `17` | ESP32 TX pin (transmits to modem RX) |
| `uart_baud` | `uint32_t` | `115200` | UART baud rate |
| `pwrkey_gpio` | `uint8_t` | `0xFF` | PWRKEY GPIO; `0xFF` = not wired |
| `sleep_gpio` | `uint8_t` | `0xFF` | DTR/sleep GPIO; `0xFF` = not wired |
| `reset_gpio` | `uint8_t` | `0xFF` | Hardware reset GPIO; `0xFF` = not wired |
| `wifi_debug_window_s` | `uint16_t` | `600` | Seconds Wi-Fi station stays active alongside cellular after boot; `0` = disabled |
| `apn` | `char[64]` | `""` | PDP context APN; required when `enabled = 1` |
| `username` | `char[32]` | `""` | Optional PAP/CHAP username; empty if not required by carrier |
| `password` | `char[64]` | `""` | Optional PAP/CHAP password |
| `sim_pin` | `char[8]` | `""` | Optional SIM PIN; empty if SIM has no PIN lock |
| `connectivity_check_host` | `char[64]` | `"8.8.8.8"` | IPv4 address to ICMP-ping after PPP connects; empty = skip check |

### Notes

- `CellularConfig` is versioned separately from `DeviceConfig` with its own magic (`0x43454C4C`) and schema version. An integrity failure resets only the cellular config to defaults.
- When `enabled = 1`, the SIM7600E modem is the primary uplink. Wi-Fi station remains active for `wifi_debug_window_s` seconds after boot, then stops automatically. The Overview page Uplink stat reflects cellular as primary.
- `connectivity_check_host` defaults to `"8.8.8.8"` in the compiled-in `CellularConfig`. When the field is emptied in the UI, the form still pre-fills `"8.8.8.8"` for convenience before save.
- `username`/`password` are used for PPP PAP authentication (`esp_netif_ppp_set_auth`). Leave empty if the carrier does not require authentication.
- `connectivity_check_host` must be an IPv4 address (not a hostname); it is pinged via ICMP after PPP connects. The result is shown in the Connection panel on the Overview page.

---

## Sensor configuration (`sensor_cfg`)

Struct: `SensorConfigList` holding up to 8 `SensorRecord` entries.

### List-level fields

| Field | Type | Default | Notes |
|-------|------|---------|-------|
| `sensor_count` | `uint16_t` | `0` | Number of active records (0–8) |
| `next_sensor_id` | `uint32_t` | `1` | Auto-increment counter; must not be 0 |

The default sensor list is **empty** — no sensors are pre-configured at first boot.

### `SensorRecord` fields

| Field | Type | Default | Constraints |
|-------|------|---------|-------------|
| `id` | `uint32_t` | assigned on add | Non-zero; unique within the list |
| `enabled` | `uint8_t` | `1` | 0 or 1 |
| `sensor_type` | `SensorType` (uint8_t) | — | Must be a recognised type |
| `transport_kind` | `TransportKind` (uint8_t) | — | Must match the sensor's supported transport |
| `poll_interval_ms` | `uint32_t` | `10000` | 5 000–3 600 000 ms |
| `i2c_bus_id` | `uint8_t` | `0` | Must be `0` (only one I2C bus) |
| `i2c_address` | `uint8_t` | Sensor descriptor default | Must be one of the descriptor's allowed I2C addresses; see table below |
| `uart_port_id` | `uint8_t` | `1` | UART sensors only: must match the fixed board binding for the selected sensor |
| `uart_rx_gpio_pin` | `int16_t` | `-1` | UART sensors only; `-1` = unused |
| `uart_tx_gpio_pin` | `int16_t` | `-1` | UART sensors only; `-1` = unused |
| `uart_baud_rate` | `uint32_t` | `9600` | UART sensors: 1 200–115 200 |
| `analog_gpio_pin` | `int16_t` | `-1` | GPIO/analog sensors; `-1` = unused |

### Per-sensor constraints

| Sensor | Transport | Default binding | Allowed addresses / pins | Min poll interval | Notes |
|--------|-----------|-----------------|--------------------------|------------------|-------|
| BME280 | I2C | Bus 0, `0x76` | `0x76`, `0x77` | 5 000 ms | — |
| BME680 | I2C | Bus 0, `0x77` | `0x76`, `0x77` | 5 000 ms | — |
| SPS30 | I2C | Bus 0, `0x69` | `0x69` | 5 000 ms | — |
| SCD30 | I2C | Bus 0, `0x61` | `0x61` | 5 000 ms | — |
| VEML7700 | I2C | Bus 0, `0x10` | `0x10` | 5 000 ms | — |
| HTU2X | I2C | Bus 0, `0x40` | `0x40` | 5 000 ms | — |
| SHT4X | I2C | Bus 0, `0x44` | `0x44` | 5 000 ms | — |
| GPS (NMEA) | UART1 | RX=GPIO18, TX=GPIO17 | Fixed Kconfig UART binding | 5 000 ms | Port, RX, TX must match Kconfig constants |
| DHT11 | GPIO | PIN_0 (GPIO4) | PIN_0/1/2 (4/5/6) | 5 000 ms | — |
| DHT22 | GPIO | PIN_0 (GPIO4) | PIN_0/1/2 (4/5/6) | 5 000 ms | — |
| DS18B20 | GPIO (1-Wire) | PIN_0 (GPIO4) | PIN_0/1/2 (4/5/6) | 5 000 ms | One device per pin only |
| ME3-NO2 | Analog | PIN_0 (GPIO4) | PIN_0/1/2 (4/5/6) | 5 000 ms | — |
| INA219 | I2C | Bus 0, `0x40` | `0x40`, `0x41`, `0x44`, `0x45` | 5 000 ms | — |
| MH-Z19B | UART2 | RX=GPIO16, TX=GPIO15 | Fixed Kconfig UART binding | 10 000 ms | Baud rate must be 9600 |

The common validation rule `poll_interval_ms ∈ [5000, 3600000]` applies to all sensors. I2C validation uses each sensor descriptor's `allowed_i2c_addresses`; address `0` is not valid for any I2C sensor and is only a zero-init placeholder.

UART bindings (port ID, RX pin, TX pin) are validated against Kconfig constants at save time. Changing pins requires a firmware rebuild, not a configuration edit.

---

## Backend configuration (`backend_cfg`)

Struct: `BackendConfigList` holding up to 4 `BackendRecord` entries.

### List-level fields

| Field | Type | Default | Constraints |
|-------|------|---------|-------------|
| `backend_count` | `uint16_t` | `4` | 0–4 |
| `next_backend_id` | `uint32_t` | `5` | Non-zero |
| `upload_interval_ms` | `uint32_t` | `145 000` | 10 000–300 000 ms |

Four backends are pre-configured by default — all **disabled**:

| ID | Type | Display name | Enabled |
|----|------|-------------|---------|
| 1 | Sensor.Community | `"Sensor.Community"` | `0` |
| 2 | Air360 API | `"Air360 API"` | `0` |
| 3 | Custom Upload | `"Custom Upload"` | `0` |
| 4 | InfluxDB | `"InfluxDB"` | `0` |

### `BackendRecord` fields

| Field | Type | Default | Constraints |
|-------|------|---------|-------------|
| `id` | `uint32_t` | assigned on add | Non-zero |
| `enabled` | `uint8_t` | `0` | 0 or 1 |
| `backend_type` | `BackendType` (uint8_t) | — | Must be a recognised type |
| `display_name` | `char[32]` | type name | 1–31 chars, non-empty |
| `device_id_override` | `char[32]` | `""` | Sensor.Community only; overrides short chip ID |
| `host` | `char[96]` | backend default | HTTP backends; host name without protocol |
| `path` | `char[96]` | backend default | HTTP backends; must start with `/` when set |
| `username` | `char[48]` | `""` | Optional Basic Auth username |
| `password` | `char[64]` | `""` | Optional Basic Auth password |
| `measurement_name` | `char[32]` | `"air360"` for InfluxDB, else `""` | InfluxDB measurement name |
| `port` | `uint16_t` | backend default | HTTP backends |
| `use_https` | `uint8_t` | backend default | `1` = HTTPS, `0` = HTTP |

### Default endpoint settings

| Backend type | `host` | `path` | `port` | `use_https` |
|-------------|--------|--------|--------|-------------|
| Sensor.Community | `api.sensor.community` | `/v1/push-sensor-data/` | `443` | `1` |
| Air360 API | `api.air360.ru` | `/v1/devices/{chip_id}/batches/{batch_id}` | `443` | `1` |
| Custom Upload | `""` | `""` | `0` | `0` |
| InfluxDB | `""` | `""` | `443` | `1` |

HTTP backends store host, path, port, and `use_https` separately in NVS. `Custom Upload` and `InfluxDB` use the same common HTTP fields; `InfluxDB` also stores `measurement_name`. On save, an omitted port becomes the selected protocol default (`443` for HTTPS, `80` for HTTP). Generated request URLs omit the port when it is the selected protocol default.

### Validation rules

**Common (all backends):**
- `id` must not be 0.
- `display_name` must be 1–31 characters, null-terminated.

**Sensor.Community:**
- `host`, `path`, `username`, `password`, and `measurement_name` must be null-terminated.
- If `enabled == 1`: `host`, `path`, and `port` must be valid.

**Air360 API:**
- `host`, `path`, `username`, `password`, and `measurement_name` must be null-terminated.
- If `enabled == 1`: `host`, `path`, and `port` must be valid.

**Custom Upload:**
- `host`, `path`, `username`, `password`, and `measurement_name` must be null-terminated.
- If `enabled == 1`: host, path, and port must be present and valid.
- The path must start with `/`.
- The port must be in range 1–65535.

**InfluxDB:**
- `host`, `path`, `username`, `password`, and `measurement_name` must be null-terminated.
- If `enabled == 1`: host, path, port, and measurement name must be present and valid.
- The path must start with `/`.
- The port must be in range 1–65535.

**List-level:**
- `upload_interval_ms` must be in range 10 000–300 000.
- `next_backend_id` must not be 0.

### `upload_interval_ms` behaviour

The upload interval applies to all backends simultaneously. Defaults to **145 seconds**. When a backend becomes due, it drains the samples that were already queued at the start of that cycle in bounded upload windows, then schedules the next cycle after the configured interval. Failed attempts retry after the configured interval. See [measurement-pipeline.md](measurement-pipeline.md) for timing details.
