# Air360 API Measurement Ingest Draft

## Status

Draft contract for the first native Air360 backend ingest endpoint.

This document describes the intended request and response shape for direct device upload.
Current implementation status should be verified against the `/backend` source tree.

## Current Scaffold Status

Based on the current backend scaffold:

- the route exists at `PUT /v1/devices/{device_id}/batches/{batch_id}`
- the route currently returns `201 Created` for accepted mock requests
- the current success response is intentionally minimal
- auth and persistence are not implemented yet
- the mock implementation currently checks:
  - `device.device_id` matches the path `device_id` when provided
  - `batch.sample_count == batch.samples.length`
  - every sample contains `sample_time_unix_ms`
  - every `sample.sensor_type` is one of the supported Air360 sensor types
  - every `values[].kind` is one of the supported Air360 measurement keys

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
- Path: `/v1/devices/{device_id}/batches/{batch_id}`
- Content-Type: `application/json`

Example:

- `PUT /v1/devices/163455989411504/batches/1743465600000`

## Authentication

Recommended first version:

- `Authorization: Bearer <user_api_key>`

The backend uses the bearer token to identify the user account.
The backend uses `device_id` in the request path to identify the device.

This keeps the first version simple:

- the user adds a device on the portal by entering its `device_id`
- the firmware only needs the user's API key
- the backend assigns a `public_id` (UUID) on registration, used for external API calls

## Core Model

The payload is batch-oriented.

- one request contains one device block
- one request contains one batch
- one batch contains many samples
- one batch may contain many samples for the same `sensor_type` at different timestamps

There is no `sensor_id` field in the contract.

For the current Air360 device model, `sensor_type` is sufficient as the stream identifier inside one device.

The device identifier in the ingest path is the numeric `device_id`. The backend also assigns a `public_id` (UUID) used on public-facing endpoints.

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
    "device_id": "163455989411504",
    "short_device_id": "3108528",
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
          { "kind": "aqi", "value": 1 },
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

- `batch_id`
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

- `device_id`
  - string
  - optional echo field
  - if present, it must match `{device_id}` from the request path

- `short_device_id`
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
  - must be one of the supported Air360 sensor types

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
  - must be one of the supported Air360 measurement keys

- `value`
  - number
  - required

## Supported Sensor Types

The current backend scaffold recognizes these sensor types:

- `bme280`
- `gps_nmea`
- `dht11`
- `dht22`
- `bme680`
- `sps30`
- `ens160`
- `me3_no2`

Implementation should be verified against `/backend/src/contracts/sensor-type.ts`.

## Supported Measurement Keys

The current backend scaffold recognizes these Air360 measurement keys:

- `temperature_c`
- `humidity_percent`
- `pressure_hpa`
- `latitude_deg`
- `longitude_deg`
- `altitude_m`
- `satellites`
- `speed_knots`
- `gas_resistance_ohms`
- `pm1_0_ug_m3`
- `pm2_5_ug_m3`
- `pm4_0_ug_m3`
- `pm10_0_ug_m3`
- `nc0_5_per_cm3`
- `nc1_0_per_cm3`
- `nc2_5_per_cm3`
- `nc4_0_per_cm3`
- `nc10_0_per_cm3`
- `typical_particle_size_um`
- `aqi`
- `tvoc_ppb`
- `eco2_ppm`
- `adc_raw`
- `voltage_mv`

Implementation should be verified against `/backend/src/contracts/measurement-kind.ts`.

## Semantics

- a batch may contain multiple samples for the same `sensor_type`
- samples are ordered by device collection order
- the backend must not assume one sample per sensor type per request
- the backend should treat `(device_id, sensor_type)` as the logical measurement stream
- the backend should preserve `sample_time_unix_ms` from the payload

## Idempotency

The endpoint is idempotent by resource path:

- `PUT /v1/devices/{device_id}/batches/{batch_id}`

Recommended backend behavior:

- if a batch with the same `batch_id` was already accepted, return success again
- do not duplicate stored samples

## Minimal Success Response

```json
{
  "accepted": true,
  "batch_id": "1743465600000",
  "accepted_samples": 4
}
```

## Example Error Response

```json
{
  "accepted": false,
  "error": {
    "code": "invalid_payload",
    "message": "batch.sample_count must equal batch.samples.length"
  }
}
```

## Initial Backend Rules

For the first implementation, keep backend behavior simple:

- accept or reject the whole batch
- do not implement partial per-sample acceptance yet
- do not require per-sample IDs
- do not require units in the payload
- require that the authenticated user has access to the device identified by `device_id`

## Portal Model

The first Air360 portal flow is intentionally simple:

1. The user opens the portal.
2. The user adds a device by entering its `device_id`.
3. The portal links that `device_id` to the user's account.
4. The firmware uses the user's API key for authenticated upload.

This means:

- one user may own many devices
- each device is identified by its `device_id`
- device ownership is enforced server-side

## Notes For Firmware Implementation

- the firmware may accumulate many samples before one upload cycle
- the `Sensor.Community` compatibility backend may still serialize only the latest compatible values
- the native Air360 backend should upload the accumulated batch as-is
