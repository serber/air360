# NVS Storage

## Status

Implemented. Keep this document aligned with the current NVS blob layouts and keys used by the firmware.

## Scope

This document explains the persistent storage schema used by the firmware, including namespaces, keys, blob layouts, schema guards, and reset behavior.

## Source of truth in code

- `firmware/main/src/config_repository.cpp`
- `firmware/main/src/config_transaction.cpp`
- `firmware/main/src/sensors/sensor_config_repository.cpp`
- `firmware/main/src/uploads/backend_config_repository.cpp`
- `firmware/main/src/cellular_config_repository.cpp`

## Read next

- [configuration-reference.md](configuration-reference.md)
- [startup-pipeline.md](startup-pipeline.md)
- [cellular-manager.md](cellular-manager.md)

The firmware uses a single NVS namespace `"air360"` for all persistent state. There are five keys stored under that namespace.

## Namespace and keys

| Key | NVS type | Stored structure | Written by |
|-----|----------|-----------------|------------|
| `device_cfg` | blob | `DeviceConfig` | `ConfigRepository`; `/config` combined save |
| `cellular_cfg` | blob | `CellularConfig` | `CellularConfigRepository`; `/config` combined save |
| `sensor_cfg` | blob | `SensorConfigList` | `SensorConfigRepository` |
| `backend_cfg` | blob | `BackendConfigList` | `BackendConfigRepository` |
| `boot_count` | u32 | `uint32_t` | `ConfigRepository` |

---

## Blob guard fields

All blob structs share the same integrity guard pattern at the start of the struct:

| Field | Type | Purpose |
|-------|------|---------|
| `magic` | `uint32_t` | Identifies the struct type |
| `schema_version` | `uint16_t` | Detects schema changes |
| `record_size` | `uint16_t` | Detects struct size changes |

On load, all three fields are validated. Any mismatch discards the stored blob and writes defaults in its place. There is no migration.

| Struct | Magic | Schema version |
|--------|-------|----------------|
| `DeviceConfig` | `0x41333630` ("A360") | 1 |
| `CellularConfig` | `0x43454C4C` ("CELL") | 1 |
| `SensorConfigList` | `0x41333631` ("A361") | 1 |
| `BackendConfigList` | `0x41333632` ("A362") | 1 |

Each boot records the observed load path for every repository in the status JSON under `config.<repository>`. Current sources are `nvs_primary`, `nvs_backup`, and `defaults`; the present implementation uses `nvs_primary` or `defaults` and leaves the backup counter at zero until backup storage is implemented. `wrote_defaults` distinguishes a successful default write from an in-memory fallback after an NVS error.

`device_cfg` and `cellular_cfg` are loaded independently at boot, but the Device Configuration page saves them together. `saveDeviceAndCellularConfig()` validates both records, stages both blobs with one NVS handle, and performs a single `nvs_commit()` after both `nvs_set_blob()` calls succeed. This prevents the web UI from committing only the device part of the form when the cellular write fails. The firmware does not yet keep dual slots or a pending transaction marker for power-loss rollback during commit.

---

## `device_cfg` — `DeviceConfig`

Device identity and network credentials.

```cpp
struct DeviceConfig {
    uint32_t magic;                  // 0x41333630
    uint16_t schema_version;         // 1
    uint16_t record_size;
    uint16_t http_port;              // default: 80
    uint8_t  lab_ap_enabled;         // 0 or 1
    uint8_t  local_auth_enabled;     // reserved, not enforced
    uint8_t  wifi_power_save_enabled;// 0 or 1
    uint8_t  ble_advertise_enabled;  // 0 or 1
    char     device_name[32];        // default: "air360"
    char     wifi_sta_ssid[33];
    char     wifi_sta_password[65];
    char     lab_ap_ssid[33];        // default: "air360"
    char     lab_ap_password[65];    // default: "air360password"
    char     sntp_server[64];        // default: "" (use pool.ntp.org)
    uint8_t  sta_use_static_ip;      // 0 = DHCP, 1 = static
    uint8_t  ble_adv_interval_index; // index into kBleAdvIntervalTable {100,300,1000,3000} ms
    uint8_t  reserved1[2];
    char     sta_ip[16];
    char     sta_netmask[16];
    char     sta_gateway[16];
    char     sta_dns[16];
};
```

| Field | Default | Notes |
|-------|---------|-------|
| `http_port` | `80` | Web server port |
| `lab_ap_enabled` | `1` | Whether setup AP is enabled by default |
| `local_auth_enabled` | `0` | Stored but not enforced in the current firmware |
| `wifi_power_save_enabled` | `0` | 0 = off; 1 = `WIFI_PS_MIN_MODEM` in station mode |
| `ble_advertise_enabled` | `0` | 0 = BLE off; 1 = BTHome v2 advertising active |
| `device_name` | `"air360"` | Also used as the DHCP hostname and BLE device name |
| `wifi_sta_ssid` | `""` | Empty string means no station credentials |
| `lab_ap_ssid` | `"air360"` | From `CONFIG_AIR360_LAB_AP_SSID` |
| `lab_ap_password` | `"air360password"` | From `CONFIG_AIR360_LAB_AP_PASSWORD` |
| `sntp_server` | `""` | Empty means use firmware default (`pool.ntp.org`) |
| `sta_use_static_ip` | `0` | 0 = DHCP; 1 = use static IP fields |
| `ble_adv_interval_index` | `2` | Index into `{100, 300, 1000, 3000}` ms; default = 1000 ms |

Compile-time defaults for AP channel and max connections are **not** stored in NVS — they are read directly from `Kconfig` constants at runtime.

---

## `cellular_cfg` — `CellularConfig`

SIM7600E modem configuration. Versioned independently of `DeviceConfig` — an integrity failure resets only this blob.

```cpp
struct CellularConfig {
    uint32_t magic;              // 0x43454C4C ("CELL")
    uint16_t schema_version;     // 1
    uint16_t record_size;
    uint8_t  enabled;            // 0 = disabled; non-zero = cellular uplink active
    uint8_t  uart_port;          // UART port number (1 or 2)
    uint8_t  uart_rx_gpio;       // ESP32 RX pin
    uint8_t  uart_tx_gpio;       // ESP32 TX pin
    uint32_t uart_baud;          // default: 115200
    uint8_t  pwrkey_gpio;        // 0xFF = not wired
    uint8_t  sleep_gpio;         // 0xFF = not wired; drives modem DTR/sleep
    uint8_t  reset_gpio;         // 0xFF = not wired
    uint8_t  reserved0;
    uint16_t wifi_debug_window_s; // seconds Wi-Fi stays up alongside cellular; default: 600
    uint16_t reserved1;
    char     apn[64];             // PDP context APN; required when enabled
    char     username[32];        // optional PAP/CHAP username; empty if not required
    char     password[64];        // optional PAP/CHAP password
    char     sim_pin[8];          // optional SIM PIN; empty if SIM has no PIN lock
    char     connectivity_check_host[64]; // IPv4 address for ICMP check; default: "8.8.8.8"
};
```

| Field | Default | Notes |
|-------|---------|-------|
| `enabled` | `0` | Non-zero enables cellular uplink and spawns the reconnect task |
| `uart_port` | `1` (UART1) | UART port for modem DTE |
| `uart_rx_gpio` | `18` | ESP32 RX pin (receives from modem TX) |
| `uart_tx_gpio` | `17` | ESP32 TX pin (transmits to modem RX) |
| `uart_baud` | `115200` | — |
| `pwrkey_gpio` | `0xFF` | `0xFF` = not wired; used for hardware power-cycle |
| `sleep_gpio` | `0xFF` | `0xFF` = not wired; asserted during reconnect backoff |
| `reset_gpio` | `0xFF` | `0xFF` = not wired; reserved, not actively used |
| `wifi_debug_window_s` | `600` | `0` = Wi-Fi station stops immediately when cellular is active |
| `apn` | `""` | Must be non-empty when `enabled != 0` |
| `connectivity_check_host` | `"8.8.8.8"` | Must be an IPv4 address (not a hostname); empty skips the check |

---

## `sensor_cfg` — `SensorConfigList`

The active sensor inventory. Holds up to `kMaxConfiguredSensors` (8) sensor records.

```cpp
struct SensorConfigList {
    uint32_t magic;           // 0x41333631
    uint16_t schema_version;  // 1
    uint16_t record_size;     // sizeof(SensorRecord)
    uint16_t sensor_count;    // number of valid records
    uint16_t reserved0;
    uint32_t next_sensor_id;  // auto-increment counter for new sensors
    SensorRecord sensors[8];
};
```

```cpp
struct SensorRecord {
    uint32_t     id;               // non-zero unique identifier
    uint8_t      enabled;          // 0 or 1
    SensorType   sensor_type;      // uint8_t enum
    TransportKind transport_kind;  // uint8_t enum
    uint32_t     poll_interval_ms; // 5000–3600000 ms
    uint8_t      i2c_bus_id;       // always 0 in current hardware
    uint8_t      i2c_address;      // 7-bit I2C address
    uint8_t      uart_port_id;     // UART_NUM_1 or UART_NUM_2
    uint8_t      reserved0;
    int16_t      analog_gpio_pin;  // -1 if unused
    int16_t      uart_rx_gpio_pin; // -1 if unused
    int16_t      uart_tx_gpio_pin; // -1 if unused
    uint32_t     uart_baud_rate;   // 1200–115200
    uint8_t      reserved1[12];
};
```

`analog_gpio_pin` stores the selected GPIO for GPIO-backed and analog-backed sensors. The allowed values are not Kconfig fields; they come from the selected sensor descriptor's `allowed_gpio_pins` list.

### `SensorType` enum values

| Value | Sensor |
|-------|--------|
| 0 | Unknown |
| 1 | BME280 |
| 2 | GPS (NMEA) |
| 3 | DHT11 |
| 4 | DHT22 |
| 5 | BME680 |
| 6 | SPS30 |
| 7 | Reserved (removed SDS011 support) |
| 8 | ME3-NO2 |
| 9 | VEML7700 |
| 10 | DS18B20 |
| 11 | SCD30 |
| 12 | HTU2X |
| 13 | SHT4X |
| 14 | INA219 |
| 15 | MH-Z19B |

### `TransportKind` enum values

| Value | Transport |
|-------|-----------|
| 0 | Unknown |
| 1 | I2C |
| 2 | Analog (ADC) |
| 3 | UART |
| 4 | GPIO |

---

## `backend_cfg` — `BackendConfigList`

Upload backend configuration. Holds up to `kMaxConfiguredBackends` (4) backend records.

```cpp
struct BackendConfigList {
    uint32_t magic;              // 0x41333632
    uint16_t schema_version;     // 1
    uint16_t record_size;        // sizeof(BackendRecord)
    uint16_t backend_count;
    uint16_t reserved0;
    uint32_t next_backend_id;    // auto-increment counter
    uint32_t upload_interval_ms; // default: 145000 ms, range: 10000–300000
    BackendRecord backends[4];
};
```

```cpp
struct BackendRecord {
    uint32_t    id;
    uint8_t     enabled;          // 0 or 1
    BackendType backend_type;     // uint8_t enum
    uint16_t    reserved0;
    char        display_name[32];
    char        host[96];               // backend host without protocol
    char        path[96];
    uint16_t    port;
    BackendProtocol protocol;           // uint8_t enum
    uint8_t     reserved1;
    BackendAuthConfig auth;             // auth type + Basic Auth credentials
    char        device_id_override[32]; // Sensor.Community: overrides Short ID
    char        measurement_name[32];   // InfluxDB only
    uint8_t     reserved2[8];
};
```

### `BackendType` enum values

| Value | Backend |
|-------|---------|
| 0 | Unknown |
| 1 | Sensor.Community |
| 2 | Air360 API |
| 3 | Custom Upload |
| 4 | InfluxDB |

### Default endpoint settings

| Backend | `host` | `path` | `port` | `use_https` |
|---------|--------|--------|--------|-------------|
| Sensor.Community | `api.sensor.community` | `/v1/push-sensor-data/` | `443` | `1` |
| Air360 API | `api.air360.ru` | `/v1/devices/{chip_id}/batches/{batch_id}` | `443` | `1` |
| Custom Upload | `""` | `""` | `0` | `0` |
| InfluxDB | `""` | `""` | `443` | `1` with default measurement `air360` |

HTTP backends store host, path, port, and `use_https` separately. `Custom Upload` uses the same common HTTP fields as the built-in backends. `InfluxDB` also stores `measurement_name`. When the web UI saves an empty port field, the stored port becomes the selected protocol default. Generated URLs omit `:443` for HTTPS and `:80` for HTTP.

---

## `boot_count` — `uint32_t`

Incremented on every boot via `nvs_get_u32` / `nvs_set_u32`. If the key does not exist yet (first boot), starts from 0 and writes 1. Shown on the Overview page and included in the Diagnostics raw JSON dump.

---

## Validation and reset behavior

All three blob repositories follow the same load pattern:

1. Open namespace `"air360"` in `NVS_READWRITE` mode.
2. Query blob size with a `nullptr` buffer call.
3. If key not found (`ESP_ERR_NVS_NOT_FOUND`): write defaults, return.
4. If stored blob size differs from `sizeof` of the struct: write defaults, return.
5. Read the blob into a local struct.
6. Validate magic, schema version, and record size.
7. If any field mismatches: write defaults, return.
8. Return the loaded struct.

There is no incremental migration. Any structural change to a stored struct (new field, renamed field, changed size) causes the stored value to be silently replaced with compiled-in defaults on the next boot.

---

## Clearing NVS

To reset all configuration to defaults, erase the NVS partition:

```bash
idf.py erase-flash
```

Or erase only the NVS partition using `esptool.py`:

```bash
esptool.py --port <PORT> erase_region 0x9000 0x6000
```

On next boot the firmware writes fresh defaults for all three blobs and resets the boot counter.

---

For field-level details, valid ranges, and validation rules enforced on save, see [configuration-reference.md](configuration-reference.md).
