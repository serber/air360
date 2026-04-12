# Upload Adapters

This document describes the two backend upload adapters — what HTTP request each one sends, how measurement data is mapped to the payload format, and how responses are interpreted.

---

## Adapter interface

Both adapters implement `IBackendUploader`:

```cpp
class IBackendUploader {
    bool validateConfig(const BackendRecord& record, string& error);
    bool buildRequests(const BackendRecord& record,
                       const MeasurementBatch& batch,
                       vector<UploadRequestSpec>& out_requests,
                       string& error);
    UploadResultClass classifyResponse(const UploadTransportResponse& response);
};
```

`buildRequests()` converts one `MeasurementBatch` into one or more `UploadRequestSpec` objects. Each spec is then executed by `UploadTransport` as an independent HTTP request.

```cpp
struct UploadRequestSpec {
    string              request_key;   // for logging
    UploadMethod        method;        // kPost or kPut
    string              url;
    vector<{name,value}> headers;
    string              body;
    int                 timeout_ms;   // 15000
};
```

`classifyResponse()` maps the transport result to `UploadResultClass`. The upload manager uses this to decide whether to acknowledge or restore the inflight queue.

---

## Sensor.Community

### Endpoint

```
POST http://api.sensor.community/v1/push-sensor-data/
```

### Grouping — one request per sensor

The batch may contain points from multiple sensors. Sensor.Community expects **one POST per physical sensor**, identified by a `X-PIN` header. The adapter groups batch points by `sensor_id + pin` and emits one request per group.

**Pin mapping:**

| Sensor type | X-PIN | Notes |
|-------------|-------|-------|
| BME280 | 11 | |
| BME680 | 11 | Same pin as BME280 |
| DHT11 | 7 | |
| DHT22 | 7 | |
| DS18B20 | 7 | |
| GPS (NMEA) | 9 | |
| SPS30 | 1 | |
| SCD30 | — | Not supported, skipped |
| HTU2X | — | Not supported, skipped |
| SHT4X | — | Not supported, skipped |
| VEML7700 | — | Not supported, skipped |
| ME3-NO2 | — | Not supported, skipped |

Sensors not in this table produce no request. If the batch contains only unsupported sensor types, `buildRequests()` returns `true` with an empty request list — the upload manager treats this as `kNoData`.

### Value type mapping

Each `MeasurementPoint` is mapped to a `value_type` string in the `sensordatavalues` array:

**BME280 / BME680 (pin 11):**

| ValueKind | value_type |
|-----------|-----------|
| `kTemperatureC` | `"temperature"` |
| `kPressureHpa` | `"pressure"` |
| `kHumidityPercent` | `"humidity"` |
| `kGasResistanceOhms` | skipped |

**DHT11 / DHT22 / DS18B20 (pin 7):**

| ValueKind | value_type |
|-----------|-----------|
| `kTemperatureC` | `"temperature"` |
| `kHumidityPercent` | `"humidity"` |

**GPS (pin 9):**

| ValueKind | value_type |
|-----------|-----------|
| `kLatitudeDeg` | `"lat"` |
| `kLongitudeDeg` | `"lon"` |
| `kAltitudeM` | `"height"` |
| other GPS fields | skipped |

**SPS30 (pin 1):**

| ValueKind | value_type |
|-----------|-----------|
| `kPm1_0UgM3` | `"P0"` |
| `kPm2_5UgM3` | `"P2"` |
| `kPm4_0UgM3` | `"P4"` |
| `kPm10_0UgM3` | `"P1"` |
| `kNc0_5PerCm3` | `"N05"` |
| `kNc1_0PerCm3` | `"N1"` |
| `kNc2_5PerCm3` | `"N25"` |
| `kNc4_0PerCm3` | `"N4"` |
| `kNc10_0PerCm3` | `"N10"` |
| `kTypicalParticleSizeUm` | `"TS"` |

Within a group, if the same `value_type` appears more than once (e.g., two temperature points for the same sensor in the same batch window), the **latest value wins** — it overwrites the previous one.

### Headers

| Header | Value | Source |
|--------|-------|--------|
| `Content-Type` | `application/json` | fixed |
| `X-Sensor` | `esp32-{chip_id}` | `short_chip_id` or `device_id_override` |
| `X-MAC-ID` | `esp32-{esp_mac_id}` | station MAC in hex |
| `X-PIN` | `{pin}` | sensor group pin number |
| `User-Agent` | `{project_version}/{chip_id}/{esp_mac_id}` | build info + identity |

**`X-Sensor` chip ID resolution:**
1. If `device_id_override` is set in `BackendRecord` → use that value
2. Otherwise use `short_chip_id` (24-bit legacy airrohr format)
3. Fallback to `chip_id` if `short_chip_id` is empty

The `short_chip_id` must match the device ID registered on `devices.sensor.community`.

### Body format

```json
{
  "software_version": "0.1.0",
  "sensordatavalues": [
    { "value_type": "temperature", "value": "24.1" },
    { "value_type": "humidity",    "value": "53.2" },
    { "value_type": "pressure",    "value": "1013.1" }
  ]
}
```

All values are formatted as **strings** (not JSON numbers) with the precision defined per `SensorValueKind`:
- Temperature, humidity, pressure, PM, NC, particle size: 1 decimal place
- Gas resistance: 0 decimal places
- Lat/lon: 6 decimal places

### Success condition

HTTP 200–208 → `kSuccess`. Anything else → `kHttpError`.

---

## Air360 API

### Endpoint

```
PUT http://api.air360.ru/v1/devices/{chip_id}/batches/{batch_id}
```

- `{chip_id}` — full 48-bit decimal chip ID (`chip_id` field from `BuildInfo`)
- `{batch_id}` — unique `uint64_t` batch identifier from `MeasurementBatch`

Trailing slashes are stripped from the base URL before constructing the path.

### One request per batch

Unlike Sensor.Community, the Air360 adapter emits exactly **one PUT request** per upload cycle, regardless of how many sensor types are in the batch. All samples and all sensor types are packed into a single JSON body.

### Extra preconditions

`buildRequests()` fails early with an error string (returns `false`) if:
- `batch.created_unix_ms <= 0` — unix time is not valid
- `batch.chip_id` and `batch.short_chip_id` are both empty

These checks are in addition to the network/time guards already applied by the upload manager.

### Grouping

Batch points are grouped by `(sensor_type, sample_time_ms)`. Each unique combination becomes one `sample` entry in the payload. Multiple values from the same sensor at the same timestamp (e.g., BME280 temperature + humidity + pressure from one poll) are collapsed into a single `values` array.

### Headers

| Header | Value |
|--------|-------|
| `Content-Type` | `application/json` |
| `User-Agent` | `air360/{project_version}` |

No authentication header is sent in the current firmware version.

### Body format

```json
{
  "schema_version": 1,
  "sent_at_unix_ms": 1744400000000,
  "device": {
    "device_name": "air360",
    "board_name": "esp32-s3-devkitc-1",
    "chip_id": "123456789012",
    "short_chip_id": "789012",
    "esp_mac_id": "aabbccddeeff",
    "firmware_version": "0.1.0"
  },
  "batch": {
    "sample_count": 2,
    "samples": [
      {
        "sensor_type": "bme280",
        "sample_time_unix_ms": 1744400000000,
        "values": [
          { "kind": "temperature_c", "value": 24.1 },
          { "kind": "humidity_percent", "value": 53.2 },
          { "kind": "pressure_hpa", "value": 1013.1 }
        ]
      },
      {
        "sensor_type": "sps30",
        "sample_time_unix_ms": 1744399995000,
        "values": [
          { "kind": "pm1_0_ug_m3", "value": 1.3 },
          { "kind": "pm2_5_ug_m3", "value": 2.1 }
        ]
      }
    ]
  }
}
```

Values are formatted as **JSON numbers** (not strings) with the same per-kind precision as Sensor.Community. `kind` strings are the `sensorValueKindKey()` identifiers from `sensor_types.hpp` (e.g., `"temperature_c"`, `"pm2_5_ug_m3"`, `"co2_ppm"`).

All sensor types are supported — the Air360 API adapter does not filter by sensor type.

### Success condition

HTTP 200–208 and **409** → `kSuccess`.

HTTP 409 means the server already has a batch with this `batch_id`. The upload manager treats it as success and acknowledges the inflight queue — the data was already delivered.

Any other HTTP status → `kHttpError`.

---

## Transport layer

Both adapters produce `UploadRequestSpec` objects that are executed by `UploadTransport::execute()`. See [upload-transport.md](upload-transport.md) for the full `esp_http_client` configuration, response struct field population, and timing details.

- HTTP client: `esp_http_client` with CRT bundle (TLS capable)
- Timeout: 15 000 ms per request
- Request/response buffer: 512 bytes
- Keep-alive: disabled

The transport returns `UploadTransportResponse`:

```cpp
struct UploadTransportResponse {
    esp_err_t   transport_err;        // ESP_OK or ESP-IDF error
    int         http_status;          // 0 if transport failed before response
    int         response_size;        // bytes received
    uint32_t    response_time_ms;     // total request duration
    uint32_t    connect_time_ms;
    uint32_t    request_send_time_ms;
    uint32_t    first_response_time_ms;
    string      body_snippet;         // used in error messages
};
```

If `transport_err != ESP_OK` (connection refused, DNS failure, timeout), `classifyResponse()` returns `kTransportError` in both adapters — regardless of what the HTTP status might say.

---

## Comparison

| Property | Sensor.Community | Air360 API |
|----------|-----------------|------------|
| Method | POST | PUT |
| Requests per cycle | One per supported sensor | One per batch |
| Payload format | String values in `sensordatavalues` | Number values in typed `samples` |
| Device identification | `X-Sensor: esp32-{short_chip_id}` | URL path: `/devices/{chip_id}` |
| Authentication | None | None |
| Supported sensors | BME280, BME680, DHT11/22, DS18B20, GPS, SPS30 | All sensor types |
| Success HTTP codes | 200–208 | 200–208, 409 |
| Extra preconditions | None | unix_ms > 0, chip_id non-empty |
