# Upload Adapters

## Status

Implemented. Keep this document aligned with the currently supported backend adapters and payload mapping logic.

## Scope

This document covers the adapter-specific layer that turns Air360 measurements into backend requests and classifies backend responses.

## Source of truth in code

- `firmware/main/src/uploads/adapters/air360_api_uploader.cpp`
- `firmware/main/src/uploads/adapters/air360_json_payload.cpp`
- `firmware/main/src/uploads/adapters/custom_upload_uploader.cpp`
- `firmware/main/src/uploads/adapters/influxdb_uploader.cpp`
- `firmware/main/src/uploads/adapters/sensor_community_uploader.cpp`
- `firmware/main/src/uploads/backend_registry.cpp`

## Read next

- [upload-transport.md](upload-transport.md)
- [measurement-pipeline.md](measurement-pipeline.md)
- [configuration-reference.md](configuration-reference.md)

This document describes the four backend upload adapters — how each backend delivers a measurement batch, how HTTP-backed adapters map payloads, and how protocol responses are interpreted.

---

## Adapter interface

Backend adapters implement `IBackendUploader`:

```cpp
class IBackendUploader {
    bool validateConfig(const BackendRecord& record, string& error);
    UploadAttemptResult deliver(const BackendRecord& record,
                                const MeasurementBatch& batch,
                                const BackendDeliveryContext& context);
};
```

`deliver()` owns protocol-specific delivery for one backend window. `UploadManager` still owns queue windows, acknowledgement cursors, retry scheduling, best-effort demotion, and pruning; the adapter returns one `UploadAttemptResult` describing whether the window was accepted, skipped, or should be retried.

`BackendDeliveryContext` currently provides the shared HTTP executor plus stop/watchdog callbacks. HTTP is therefore an implementation detail of the current adapters, not a required shape for future backends. A future MQTT, TCP, filesystem, or modem-specific backend can use a different service through the context while preserving the same queue and acknowledgement policy.

```cpp
struct UploadAttemptResult {
    UploadResultClass result;
    UploadAttemptPhase phase;          // preflight, registration, data upload
    esp_err_t          transport_err;
    int                status_code;    // HTTP status or protocol-specific code
    uint32_t           response_time_ms;
    uint32_t           retry_after_seconds;
    string             message;
};
```

HTTP-backed adapters still build one or more internal `UploadRequestSpec` objects and execute them with `UploadTransport`:

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

Adapters map transport/protocol responses to `UploadResultClass`. `UploadManager` applies that result per backend delivery window: `kSuccess` and `kNoData` advance only that backend cursor, while failures keep only that backend window for retry. Backend-specific failures also feed the best-effort demotion policy documented in [measurement-pipeline.md](measurement-pipeline.md); shared `kNoNetwork` failures do not.

---

## Sensor.Community

### Endpoint

```
POST {scheme}://{host}{:port}{path}
```

Default value: `https://api.sensor.community/v1/push-sensor-data/`

The `:port` segment is included only when the configured port is not the protocol default (`443` for HTTPS, `80` for HTTP).

### Grouping — one request per sensor

The batch may contain points from multiple sensors. Sensor.Community expects **one POST per physical sensor**, identified by a `X-PIN` header. The adapter groups batch points by `sensor_id + pin` and emits one request per group.

**Pin mapping (complete list of sensor types currently registered in firmware):**

| Sensor type | Transport | X-PIN | Sensor.Community handling |
|-------------|-----------|-------|---------------------------|
| BME280 | I2C | 11 | Sent as climate data |
| BME680 | I2C | 11 | Sent as climate data; gas resistance is skipped |
| SHT3X | I2C | 7 | Sent as temperature + humidity |
| SHT4X | I2C | 7 | Sent as temperature + humidity |
| HTU2X | I2C | 7 | Sent as temperature + humidity |
| DHT11 | GPIO | 7 | Sent as temperature + humidity |
| DHT22 | GPIO | 7 | Sent as temperature + humidity |
| DS18B20 | GPIO (1-Wire) | 7 | Sent as temperature only |
| SCD30 | I2C | 17 | Sent as temperature + humidity + CO2 |
| VEML7700 | I2C | — | Not supported, skipped |
| SPS30 | I2C | 1 | Sent as particulate matter data |
| SDS011 | UART | 1 | Sent as PM2.5 and PM10 particulate matter data |
| GPS (NMEA) | UART1 | 9 | Sent as `lat` / `lon` / `height`; other GPS fields are skipped |
| ME3-NO2 | Analog (ADC) | — | Not supported, skipped |

Sensors not in this table produce no request. If the batch contains only unsupported sensor types, `deliver()` returns `kNoData`.

### Value type mapping

Each `MeasurementPoint` is mapped to a `value_type` string in the `sensordatavalues` array:

**BME280 / BME680 (pin 11):**

| ValueKind | value_type |
|-----------|-----------|
| `kTemperatureC` | `"temperature"` |
| `kPressureHpa` | `"pressure"` |
| `kHumidityPercent` | `"humidity"` |
| `kGasResistanceOhms` | skipped |

**DHT11 / DHT22 (pin 7):**

| ValueKind | value_type |
|-----------|-----------|
| `kTemperatureC` | `"temperature"` |
| `kHumidityPercent` | `"humidity"` |

**HTU2X / SHT3X / SHT4X (pin 7):**

| ValueKind | value_type |
|-----------|-----------|
| `kTemperatureC` | `"temperature"` |
| `kHumidityPercent` | `"humidity"` |

**DS18B20 (pin 7):**

| ValueKind | value_type |
|-----------|-----------|
| `kTemperatureC` | `"temperature"` |

**SCD30 (pin 17):**

| ValueKind | value_type |
|-----------|-----------|
| `kTemperatureC` | `"temperature"` |
| `kHumidityPercent` | `"humidity"` |
| `kCo2Ppm` | `"co2_ppm"` |

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

**SDS011 (pin 1):**

| ValueKind | value_type |
|-----------|-----------|
| `kPm2_5UgM3` | `"P2"` |
| `kPm10_0UgM3` | `"P1"` |

Within a group, if the same `value_type` appears more than once (e.g., two temperature points for the same sensor in the same batch window), the **latest value wins** — it overwrites the previous one.

### Headers

| Header | Value | Source |
|--------|-------|--------|
| `Content-Type` | `application/json` | fixed |
| `X-Sensor` | `esp32-{device_id}` | `short_device_id` or `device_id_override` |
| `X-MAC-ID` | `esp32-{esp_mac_id}` | station MAC in hex |
| `X-PIN` | `{pin}` | sensor group pin number |
| `User-Agent` | `{project_version}/{device_id}/{esp_mac_id}` | build info + identity |

**`X-Sensor` device ID resolution:**
1. If `device_id_override` is set in `BackendRecord` → use that value
2. Otherwise use `short_device_id` (24-bit legacy airrohr format)
3. Fallback to `device_id` if `short_device_id` is empty

The `short_device_id` must match the device ID registered on `devices.sensor.community`.

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
PUT {scheme}://{host}{:port}{path}
```

- `{device_id}` — full 48-bit decimal device ID (`device_id` field from `BuildInfo`)
- `{batch_id}` — unique `uint64_t` batch identifier from `MeasurementBatch`
- Default value: `https://api.air360.ru/v1/devices/{device_id}/batches/{batch_id}`
- Stored URLs may still contain the legacy base form `http(s)://api.air360.ru`; when that is loaded, the adapter appends `/v1/devices/{device_id}/batches/{batch_id}` for backward compatibility.
- The `:port` segment is included only when the configured port is not the protocol default (`443` for HTTPS, `80` for HTTP).

### Device registration

Before the first upload cycle, the Air360 API adapter runs an internal registration step from `deliver()` to register the device with the backend. This step runs once per firmware boot and sets a `registered_` atomic flag on success; subsequent cycles skip it.

The adapter requires an Air360 upload secret stored in the separate
`air360_cred` NVS namespace. The Backends page can generate a new secret or let
the user paste a previously saved one. Once stored, the page shows a configured
state with a masked preview and requires an explicit replacement action before
submitting a new secret. Registration sends only `upload_secret_hash`; batch
ingest sends the raw secret as a bearer token.

**Registration request:**

```
PUT {scheme}://{host}{:port}/v1/devices/{device_id}/register
```

```json
{
  "schema_version": 1,
  "name": "<device_name>",
  "firmware_version": "0.1.0",
  "location": {
    "latitude": 55.751244,
    "longitude": 37.618423
  },
  "upload_secret_hash": "sha256:base64url-sha256-value"
}
```

- If `latitude` and `longitude` are both `0.0`, `deliver()` returns `kConfigError` during the registration phase and the upload cycle is skipped. Set coordinates on the Backends page before enabling Air360 API.
- If the upload secret is missing or invalid, `deliver()` returns `kConfigError`; generate or enter the secret on the Backends page.
- HTTP 2xx → device registered, `registered_` set to `true`.
- Transport error or non-2xx HTTP → error is logged, the cycle is counted as a transport error, and registration retries on the next upload cycle.

### One request per batch

Unlike Sensor.Community, the Air360 adapter emits exactly **one PUT request** per upload cycle, regardless of how many sensor types are in the batch. All samples and all sensor types are packed into a single JSON body.

### Extra preconditions

`deliver()` fails early with `kConfigError` if:
- `batch.created_unix_ms <= 0` — unix time is not valid
- `batch.device_id` and `batch.short_device_id` are both empty

These checks are in addition to the network/time guards already applied by the upload manager.

### Grouping

Batch points are grouped by `(sensor_type, sample_time_ms)`. Each unique combination becomes one `sample` entry in the payload. Multiple values from the same sensor at the same timestamp (e.g., BME280 temperature + humidity + pressure from one poll) are collapsed into a single `values` array.

### Headers

| Header | Value |
|--------|-------|
| `Content-Type` | `application/json` |
| `User-Agent` | `air360/{project_version}` |
| `Authorization` | `Bearer <upload_secret>` |

### Body format

```json
{
  "schema_version": 1,
  "sent_at_unix_ms": 1744400000000,
  "device": {
    "device_name": "air360",
    "board_name": "esp32-s3-devkitc-1",
    "device_id": "123456789012",
    "short_device_id": "789012",
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

## Custom Upload

### Endpoint

```
POST {scheme}://{host}{:port}{path}
```

- No compiled-in default URL is provided.
- The user must enter host and path before enabling the backend.
- The `:port` segment is included only when the configured port is not the protocol default (`443` for HTTPS, `80` for HTTP).

### One request per batch

Like `Air360 API`, this adapter emits exactly **one request per upload cycle** and includes all supported sensor types in a single JSON body.

### Payload and preconditions

`Custom Upload` reuses the same shared Air360 JSON payload builder as `Air360 API`. That means it has the same behavior for:

- preconditions: `batch.created_unix_ms > 0` and at least one of `batch.device_id` / `batch.short_device_id` must be present
- grouping: points are grouped by `(sensor_type, sample_time_ms)`
- value formatting: JSON numbers with per-kind precision from `sensorValueKindPrecision()`
- sensor coverage: all sensor types are passed through without filtering

### Headers

| Header | Value |
|--------|-------|
| `Content-Type` | `application/json` |
| `User-Agent` | `air360/{project_version}` |

### Body format

The JSON body is identical to the `Air360 API` body shown above. Only the HTTP method and destination URL differ.

### Success condition

HTTP 200–208 and **409** → `kSuccess`.

Any other HTTP status → `kHttpError`.

---

## InfluxDB

### Endpoint

```
POST {scheme}://{host}{:port}{path}
```

- The URL is built from the InfluxDB form fields on the Backends page.
- No compiled-in host or path is provided.
- The default measurement name is `air360`.
- The `:port` segment is included only when the configured port is not the protocol default (`443` for HTTPS, `80` for HTTP).

### One request per batch

The adapter emits exactly **one POST per upload cycle**. The body contains multiple Influx line protocol rows, one row per grouped sample.

### Grouping

Batch points are grouped by `(sensor_id, sensor_type, sample_time_ms)`.

Each group becomes one line:
- measurement: configured `measurement_name`
- tags: `node`, `sensor_type`, `sensor_id`
- fields: one field per `SensorValueKind`
- timestamp: `sample_time_ms` converted to nanoseconds

If the same `SensorValueKind` appears more than once in one group, the latest value wins.

### Headers

| Header | Value |
|--------|-------|
| `Content-Type` | `application/x-www-form-urlencoded` |
| `User-Agent` | `air360/{project_version}` |
| `Authorization` | `Basic ...` when username or password is configured |

The `Content-Type` follows the historical airrohr InfluxDB integration.

### Body format

Example:

```text
air360,node=789012,sensor_type=bme280,sensor_id=1 temperature_c=24.1,humidity_percent=53.2,pressure_hpa=1013.1 1744400000000000000
air360,node=789012,sensor_type=sps30,sensor_id=2 pm1_0_ug_m3=1.3,pm2_5_ug_m3=2.1 1744399995000000000
```

Field names use `sensorValueKindKey()`. Numeric values are sent without quotes.

### Extra preconditions

- `batch.created_unix_ms > 0`
- at least one measurement point is present
- valid InfluxDB configuration when the backend is enabled

### Success condition

HTTP 200–208 → `kSuccess`.

Any other HTTP status → `kHttpError`.

---

## Transport layer

The current adapters are HTTP-backed and execute internal `UploadRequestSpec` objects with `UploadTransport::execute()`. This is no longer part of the cross-backend interface; it is the shared helper used by HTTP backends. See [upload-transport.md](upload-transport.md) for the full `esp_http_client` configuration, response struct field population, and timing details.

- HTTP client: `esp_http_client` with CRT bundle (TLS capable)
- Timeout: 15 000 ms per request
- RX buffer: 2 048 bytes
- TX buffer: 1 024 bytes
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

If `transport_err != ESP_OK` (connection refused, DNS failure, timeout), HTTP-backed adapters return `kTransportError` in their `UploadAttemptResult` — regardless of what the HTTP status might say.

---

## Comparison

| Property | Sensor.Community | Air360 API | Custom Upload | InfluxDB |
|----------|-----------------|------------|---------------|----------|
| Method | POST | PUT | POST | POST |
| Requests per cycle | One per supported sensor | One per batch | One per batch | One per batch |
| Payload format | String values in `sensordatavalues` | Number values in typed `samples` | Same Air360 JSON body as `Air360 API` | Influx line protocol |
| Device identification | `X-Sensor: esp32-{short_device_id}` | URL path: `/devices/{device_id}` | Device block inside JSON body | `node` tag plus `sensor_type` / `sensor_id` tags |
| Authentication | None | Bearer upload secret | None | Optional Basic Auth |
| Supported sensors | BME280, BME680, DHT11/22, HTU2X, SHT3X, SHT4X, DS18B20, SCD30, GPS, SPS30, SDS011 | All sensor types | All sensor types | All sensor types |
| Success HTTP codes | 200–208 | 200–208, 409 | 200–208, 409 | 200–208 |
| Extra preconditions | None | unix_ms > 0, device_id non-empty | unix_ms > 0, device_id non-empty | unix_ms > 0, valid Influx config |
