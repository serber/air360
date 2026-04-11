# NVS Storage

The firmware uses a single NVS namespace `"air360"` for all persistent state. There are four keys stored under that namespace.

## Namespace and keys

| Key | NVS type | Stored structure | Written by |
|-----|----------|-----------------|------------|
| `device_cfg` | blob | `DeviceConfig` | `ConfigRepository` |
| `sensor_cfg` | blob | `SensorConfigList` | `SensorConfigRepository` |
| `backend_cfg` | blob | `BackendConfigList` | `BackendConfigRepository` |
| `boot_count` | u32 | `uint32_t` | `ConfigRepository` |

---

## Blob guard fields

All three blob structs share the same integrity guard pattern at the start of the struct:

| Field | Type | Purpose |
|-------|------|---------|
| `magic` | `uint32_t` | Identifies the struct type |
| `schema_version` | `uint16_t` | Detects schema changes |
| `record_size` | `uint16_t` | Detects struct size changes |

On load, all three fields are validated. Any mismatch discards the stored blob and writes defaults in its place. There is no migration — the stored value is replaced wholesale.

| Struct | Magic | Schema version |
|--------|-------|----------------|
| `DeviceConfig` | `0x41333630` ("A360") | 2 |
| `SensorConfigList` | `0x41333631` ("A361") | 3 |
| `BackendConfigList` | `0x41333632` ("A362") | 3 |

---

## `device_cfg` — `DeviceConfig`

Device identity and network credentials.

```cpp
struct DeviceConfig {
    uint32_t magic;               // 0x41333630
    uint16_t schema_version;      // 2
    uint16_t record_size;
    uint16_t http_port;           // default: 80
    uint8_t  lab_ap_enabled;      // 0 or 1
    uint8_t  local_auth_enabled;  // reserved, not enforced
    uint16_t reserved0;
    char     device_name[32];     // default: "air360"
    char     wifi_sta_ssid[33];
    char     wifi_sta_password[65];
    char     lab_ap_ssid[33];     // default: "air360"
    char     lab_ap_password[65]; // default: "air360password"
};
```

| Field | Default | Notes |
|-------|---------|-------|
| `http_port` | `80` | Web server port |
| `lab_ap_enabled` | `1` | Whether setup AP is enabled by default |
| `local_auth_enabled` | `0` | Stored but not enforced in the current firmware |
| `device_name` | `"air360"` | Also used as the DHCP hostname (lowercased, alphanumeric) |
| `wifi_sta_ssid` | `""` | Empty string means no station credentials |
| `lab_ap_ssid` | `"air360"` | From `CONFIG_AIR360_LAB_AP_SSID` |
| `lab_ap_password` | `"air360password"` | From `CONFIG_AIR360_LAB_AP_PASSWORD` |

Compile-time defaults for AP channel and max connections are **not** stored in NVS — they are read directly from `Kconfig` constants at runtime.

---

## `sensor_cfg` — `SensorConfigList`

The active sensor inventory. Holds up to `kMaxConfiguredSensors` (8) sensor records.

```cpp
struct SensorConfigList {
    uint32_t magic;           // 0x41333631
    uint16_t schema_version;  // 3
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
| 8 | ME3-NO2 |
| 9 | VEML7700 |
| 10 | DS18B20 |
| 11 | SCD30 |
| 12 | HTU2X |
| 13 | SHT4X |

Note: value `7` is not assigned (was ENS160, removed).

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
    uint16_t schema_version;     // 3
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
    char        device_id_override[32]; // Sensor.Community: overrides Short ID
    char        endpoint_url[160];      // static default per backend type
    char        bearer_token[160];      // reserved, not used in current firmware
    uint8_t     reserved1[8];
};
```

### `BackendType` enum values

| Value | Backend |
|-------|---------|
| 0 | Unknown |
| 1 | Sensor.Community |
| 2 | Air360 API |

### Default endpoint URLs

| Backend | Default `endpoint_url` |
|---------|----------------------|
| Sensor.Community | `http://api.sensor.community/v1/push-sensor-data/` |
| Air360 API | `http://api.air360.ru` |

Endpoint URLs are written into the record when defaults are applied. They are stored in NVS but the current UI does not expose them for editing.

---

## `boot_count` — `uint32_t`

Incremented on every boot via `nvs_get_u32` / `nvs_set_u32`. If the key does not exist yet (first boot), starts from 0 and writes 1. Shown on the Overview page and included in `/status` JSON.

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
