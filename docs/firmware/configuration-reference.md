# Configuration Reference

The firmware stores all user-editable configuration in three NVS blobs. This document is a field-by-field reference for each configuration domain — defaults, valid ranges, and validation rules enforced at save time.

For storage format details (magic numbers, schema versions, struct layouts) see [nvs.md](nvs.md).

---

## Configuration domains

| Domain | NVS key | What it controls |
|--------|---------|-----------------|
| Device | `device_cfg` | Network credentials, device identity, web server port |
| Sensors | `sensor_cfg` | Which sensors are active and how each is polled |
| Backends | `backend_cfg` | Upload destinations and upload interval |

All three are loaded at boot step 4–6, validated, and replaced with compiled-in defaults on any integrity failure. There is no migration — a schema change wipes stored values.

---

## How to change configuration

Configuration is changed through the embedded web UI served on port 80, or via the HTTP API used by the web UI (see [web-ui.md](web-ui.md)). The three pages map directly to the three domains:

| Web UI page | Domain |
|-------------|--------|
| Device | `device_cfg` |
| Sensors | `sensor_cfg` |
| Backends | `backend_cfg` |

Changes are written to NVS immediately on save. The device must be rebooted for most changes to take effect (network credentials, sensor list). Backend and upload interval changes are applied by the upload manager without a reboot.

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
| `CONFIG_AIR360_GPIO_SENSOR_PIN_0/1/2` | `4` / `5` / `6` | Valid GPIO slots for GPIO/analog sensors |

AP channel and max-connections are read from Kconfig at runtime and are **not** written to NVS.

---

## Device configuration (`device_cfg`)

Struct: `DeviceConfig`

### Fields

| Field | Type | Default | Constraints |
|-------|------|---------|-------------|
| `http_port` | `uint16_t` | `80` | 1–65535; must be non-zero |
| `lab_ap_enabled` | `uint8_t` | `1` | 0 or 1 |
| `local_auth_enabled` | `uint8_t` | `0` | Stored but not enforced |
| `device_name` | `char[32]` | `"air360"` | 1–31 chars, non-empty, null-terminated |
| `wifi_sta_ssid` | `char[33]` | `""` | 0–32 chars; empty = no station config |
| `wifi_sta_password` | `char[65]` | `""` | 0–63 chars |
| `lab_ap_ssid` | `char[33]` | `"air360"` | 1–32 chars, non-empty |
| `lab_ap_password` | `char[65]` | `"air360password"` | 0 chars (open) or 8–63 chars |

### Validation rules (`ConfigRepository::isValid`)

- `http_port` must not be 0.
- `device_name` must not be empty.
- All string fields must be null-terminated within their buffer.
- `wifi_sta_ssid` length ≤ 32 (empty is valid — triggers setup AP mode at boot).
- `wifi_sta_password` length ≤ 63.
- `lab_ap_ssid` must be 1–32 characters.
- `lab_ap_password`: either empty (open AP) or 8–63 characters (WPA2-PSK).

### Notes

- `device_name` is used as the DHCP hostname: lowercased, non-alphanumeric characters replaced with `-`, trailing hyphens stripped. Falls back to `"air360"` if the result is empty.
- `lab_ap_enabled` is stored but the current firmware always starts the AP when station connection fails, regardless of this field.
- `local_auth_enabled` is reserved; the web server does not enforce it.

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
| `i2c_address` | `uint8_t` | `0x77` | Sensor-specific; see table below |
| `uart_port_id` | `uint8_t` | `1` | GPS only: must match Kconfig |
| `uart_rx_gpio_pin` | `int16_t` | `-1` | GPS only; `-1` = unused |
| `uart_tx_gpio_pin` | `int16_t` | `-1` | GPS only; `-1` = unused |
| `uart_baud_rate` | `uint32_t` | `9600` | GPS: 1 200–115 200 |
| `analog_gpio_pin` | `int16_t` | `-1` | GPIO/analog sensors; `-1` = unused |

### Per-sensor constraints

| Sensor | Transport | Address / Pin | Min poll interval | Notes |
|--------|-----------|---------------|------------------|-------|
| BME280 | I2C | `0x76` or `0x77` | 5 000 ms | — |
| BME680 | I2C | `0x76` or `0x77` | 5 000 ms | — |
| SPS30 | I2C | `0x69` (fixed) | 5 000 ms | — |
| SCD30 | I2C | `0x61` (fixed) | 5 000 ms | — |
| VEML7700 | I2C | `0x10` (fixed) | 5 000 ms | — |
| HTU2X | I2C | `0x40` (fixed) | 5 000 ms | — |
| SHT4X | I2C | `0x44` (fixed) | 5 000 ms | — |
| GPS (NMEA) | UART | RX=18, TX=17, port=1 | 5 000 ms | Port, RX, TX must match Kconfig constants |
| DHT11 | GPIO | PIN_0/1/2 (4/5/6) | 5 000 ms | — |
| DHT22 | GPIO | PIN_0/1/2 (4/5/6) | 5 000 ms | — |
| DS18B20 | GPIO (1-Wire) | PIN_0/1/2 (4/5/6) | 5 000 ms | One device per pin only |
| ME3-NO2 | Analog | PIN_0/1/2 (4/5/6) | 5 000 ms | — |

The common validation rule `poll_interval_ms ∈ [5000, 3600000]` applies to all sensors. I2C address `0` is not valid for any sensor — it is a zero-init placeholder.

GPS UART bindings (port ID, RX pin, TX pin) are validated against the Kconfig constants at save time. Changing the GPS pins requires a firmware rebuild, not a configuration edit.

---

## Backend configuration (`backend_cfg`)

Struct: `BackendConfigList` holding up to 4 `BackendRecord` entries.

### List-level fields

| Field | Type | Default | Constraints |
|-------|------|---------|-------------|
| `backend_count` | `uint16_t` | `2` | 0–4 |
| `next_backend_id` | `uint32_t` | `3` | Non-zero |
| `upload_interval_ms` | `uint32_t` | `145 000` | 10 000–300 000 ms |

Two backends are pre-configured by default — both **disabled**:

| ID | Type | Display name | Enabled |
|----|------|-------------|---------|
| 1 | Sensor.Community | `"Sensor.Community"` | `0` |
| 2 | Air360 API | `"Air360 API"` | `0` |

### `BackendRecord` fields

| Field | Type | Default | Constraints |
|-------|------|---------|-------------|
| `id` | `uint32_t` | assigned on add | Non-zero |
| `enabled` | `uint8_t` | `0` | 0 or 1 |
| `backend_type` | `BackendType` (uint8_t) | — | Must be a recognised type |
| `display_name` | `char[32]` | type name | 1–31 chars, non-empty |
| `device_id_override` | `char[32]` | `""` | Sensor.Community only; overrides short chip ID |
| `endpoint_url` | `char[160]` | type default | Non-empty when `enabled == 1` |
| `bearer_token` | `char[160]` | `""` | Reserved; not used in current firmware |

### Default endpoint URLs

| Backend type | Default `endpoint_url` |
|-------------|----------------------|
| Sensor.Community | `http://api.sensor.community/v1/push-sensor-data/` |
| Air360 API | `http://api.air360.ru` |

Endpoint URLs are written into the record when defaults are applied. The current web UI does not expose them for editing.

### Validation rules

**Common (all backends):**
- `id` must not be 0.
- `display_name` must be 1–31 characters, null-terminated.

**Sensor.Community:**
- `endpoint_url` must be null-terminated.
- If `enabled == 1`: `endpoint_url` must not be empty.

**Air360 API:**
- `endpoint_url` must be null-terminated.
- If `enabled == 1`: `endpoint_url` must not be empty.

**List-level:**
- `upload_interval_ms` must be in range 10 000–300 000.
- `next_backend_id` must not be 0.

### `upload_interval_ms` behaviour

The upload interval applies to all backends simultaneously. Defaults to **145 seconds**. When the upload queue has a backlog (pending samples after a successful upload), the next cycle fires after `min(upload_interval_ms, 5000 ms)` to drain the queue faster. See [measurement-pipeline.md](measurement-pipeline.md) for timing details.
