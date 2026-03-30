# Air360 API Measurement Ingest Draft

## Status

Draft contract for the first native Air360 backend.

This document defines the proposed request and response shape for direct device upload.

## Goal

Provide one native ingest endpoint that:

- accepts batch upload from one Air360 device
- carries explicit device identity
- preserves per-sample timestamps
- uses generic measurement keys instead of legacy Sensor.Community field names
- supports multiple samples in one request
- keeps device bootstrap simple for the first Air360 portal version

## Endpoint

- Method: `PUT`
- Path: `/api/v1/devices/{chip_id}/batches/{client_batch_id}`
- Content-Type: `application/json`

Example:

- `PUT /api/v1/devices/163455989411504/batches/1743465600000`

## Authentication

Recommended first version:

- `Authorization: Bearer <user_api_key>`

The backend uses the bearer token to identify the user account.
The backend uses `chip_id` in the request path to identify the device.

This keeps the first version simple:

- the user adds a device on the portal by entering its `chip_id`
- the firmware only needs the user's API key
- there is no separate Air360 `device_id` in v1

## Core Model

The payload is batch-oriented.

- One request contains one device block.
- One request contains one batch.
- One batch contains many samples.
- One batch may contain many samples for the same `sensor_type` at different timestamps.

There is no `sensor_id` field in the contract.

For the current Air360 device model, `sensor_type` is sufficient as the stream identifier inside one device.

There is also no separate `device_id` field in v1.

The device identifier is the `chip_id` from the request path.

## Request Headers

- `Authorization: Bearer <user_api_key>`
- `Content-Type: application/json`
- `User-Agent: air360/<firmware_version>`

## Request Body

```json
{
  "schema_version": 1,
  "sent_at_unix_ms": 1743465605123,
  "device": {
    "device_name": "air360-lab",
    "board_name": "esp32-s3-devkitc-1",
    "chip_id": "163455989411504",
    "short_chip_id": "3108528",
    "esp_mac_id": "94a9902f6eb0",
    "firmware_version": "427df8c-dirty"
  },
  "batch": {
    "sample_count": 4,
    "samples": [
      {
        "sensor_type": "bme680",
        "sample_time_unix_ms": 1743465599000,
        "values": [
          { "kind": "temperature_c", "value": 25.9 },
          { "kind": "humidity_percent", "value": 38.2 },
          { "kind": "pressure_hpa", "value": 1004.3 },
          { "kind": "gas_resistance_ohms", "value": 108204 }
        ]
      },
      {
        "sensor_type": "bme680",
        "sample_time_unix_ms": 1743465600000,
        "values": [
          { "kind": "temperature_c", "value": 26.0 },
          { "kind": "humidity_percent", "value": 38.1 },
          { "kind": "pressure_hpa", "value": 1004.2 },
          { "kind": "gas_resistance_ohms", "value": 107980 }
        ]
      },
      {
        "sensor_type": "ens160",
        "sample_time_unix_ms": 1743465600000,
        "values": [
          { "kind": "aqi_index", "value": 1 },
          { "kind": "tvoc_ppb", "value": 27 },
          { "kind": "eco2_ppm", "value": 406 }
        ]
      },
      {
        "sensor_type": "sps30",
        "sample_time_unix_ms": 1743465600000,
        "values": [
          { "kind": "pm1_0_ug_m3", "value": 3.2 },
          { "kind": "pm2_5_ug_m3", "value": 4.8 },
          { "kind": "pm10_0_ug_m3", "value": 7.1 }
        ]
      }
    ]
  }
}
```

## Field Rules

### Top level

- `schema_version`
  - integer
  - required
  - initial value: `1`

- `client_batch_id`
  - string
  - required
  - unique per batch from one device
  - comes from the request path
  - used for idempotency

- `sent_at_unix_ms`
  - integer
  - required
  - time when firmware sent the request

### `device`

- `device_name`
  - string
  - optional but recommended

- `board_name`
  - string
  - optional but recommended

- `chip_id`
  - string
  - optional echo field
  - if present, it must match `{chip_id}` from the request path

- `short_chip_id`
  - string
  - optional

- `esp_mac_id`
  - string
  - optional

- `firmware_version`
  - string
  - required

### `batch`

- `sample_count`
  - integer
  - required
  - must match `samples.length`

- `samples`
  - array
  - required
  - must contain at least one sample

### `samples[]`

- `sensor_type`
  - string
  - required
  - examples: `bme280`, `bme680`, `ens160`, `sps30`, `gps_nmea`, `dht22`

- `sample_time_unix_ms`
  - integer
  - required
  - measurement timestamp from the device

- `values`
  - array
  - required
  - contains one or more values from the same sensor sample

### `values[]`

- `kind`
  - string
  - required
  - uses Air360 internal measurement keys

- `value`
  - number
  - required

## Semantics

- A batch may contain multiple samples for the same `sensor_type`.
- Samples are ordered by device collection order.
- The backend must not assume one sample per sensor type per request.
- The backend should treat `(chip_id, sensor_type)` as the logical measurement stream.
- The backend should preserve `sample_time_unix_ms` from the payload.

## Idempotency

The endpoint is idempotent by resource path:

- `PUT /api/v1/devices/{chip_id}/batches/{client_batch_id}`

Recommended backend behavior:

- if a batch with the same `client_batch_id` was already accepted, return success again
- do not duplicate stored samples

## Success Response

```json
{
  "accepted": true,
  "client_batch_id": "air360-3108528-1743465600000",
  "accepted_samples": 4,
  "server_time_unix_ms": 1743465605301
}
```

## Error Response

```json
{
  "accepted": false,
  "error": {
    "code": "invalid_payload",
    "message": "sample_count does not match samples length",
    "retryable": false
  },
  "server_time_unix_ms": 1743465605301
}
```

## Initial Backend Rules

For the first implementation, keep backend behavior simple:

- accept or reject the whole batch
- do not implement partial per-sample acceptance yet
- do not require per-sample IDs
- do not require units in the payload
- require that the authenticated user has access to the device identified by `chip_id`

## Portal Model

The first Air360 portal flow is intentionally simple:

1. The user opens the portal.
2. The user adds a device by entering its `chip_id`.
3. The portal links that `chip_id` to the user's account.
4. The firmware uses the user's API key for authenticated upload.

This means:

- one user may own many devices
- each device is identified by its `chip_id`
- device ownership is enforced server-side

## Notes for Firmware Implementation

- The firmware may accumulate many samples before one upload cycle.
- The `Sensor.Community` compatibility backend may still serialize only the latest compatible values.
- The native Air360 backend should upload the accumulated batch as-is.
