# Air360 Backend Docs

## Scope

This directory contains the documentation for the native Air360 API backend.

The backend implementation lives in `/backend`. These documents describe the
backend API, data model, local development flow, architecture notes, and Ubuntu
deployment flow. They do not describe the ESP-IDF firmware implementation in
`firmware/`, and they do not define the separate frontend application.

## Source Of Truth

- `/backend` is the source of truth for currently implemented backend behavior.
- `docs/backend/` contains backend documentation, API notes, deployment guides,
  and design context.

If a document here conflicts with the current backend source tree, verify the
behavior against `/backend`.

## Related Documents

| Document | Purpose |
|----------|---------|
| [adr/README.md](adr/README.md) | Backend architecture decision records |
| [backend-stack-decision.md](backend-stack-decision.md) | Backend-only stack direction and scaffold boundaries |
| [backend-ubuntu-deployment-guide.md](backend-ubuntu-deployment-guide.md) | Standalone Ubuntu deployment guide |
| [air360-backend.service](air360-backend.service) | `systemd` unit example |
| [air360-backend.nginx.conf](air360-backend.nginx.conf) | `nginx` site config example |
| [air360-backend-release-deploy.sh](air360-backend-release-deploy.sh) | Release update script for the Ubuntu deployment flow |

## Overview

The Air360 API backend is a Fastify-based service for device registration,
sensor measurement ingest, and latest-reading queries.

## Stack

- Runtime: Node.js >= 20
- Framework: Fastify 5
- Language: TypeScript
- Database: PostgreSQL 18 + TimescaleDB 2
- Query builder: Kysely
- Driver: pg / node-postgres

## Source Layout

```text
backend/
  src/
    server.ts              - entry point, starts HTTP server
    app.ts                 - builds Fastify instance, registers config decorator
    config/env.ts          - environment variable parsing
    plugins/               - shared Fastify setup
    routes/
      v1/
        devices.ts         - device registration and latest readings
        ingest.ts          - sensor batch ingest
    modules/
      devices/
        device-repository.ts      - device DB operations
      ingest/
        ingest-repository.ts      - batch and measurement DB operations
      measurements/
        measurement-repository.ts - measurement read queries
    db/
      client.ts            - Kysely singleton, BIGINT parsed as number
      schema.ts            - TypeScript types for DB tables
    contracts/
      sensor-type.ts       - supported sensor types
      measurement-kind.ts  - supported measurement kinds
  migrations/              - plain SQL migration files
  scripts/
    migrate.ts             - migration runner
```

## Commands

Run commands from `/backend`.

```bash
npm install
npm run dev        # development server with hot reload
npm run build      # compile to dist/
npm start          # run compiled build
npm run typecheck  # type check without emitting
npm run migrate    # apply pending DB migrations
```

## Environment

Copy `/backend/.env.example` to `/backend/.env`:

```text
HOST=0.0.0.0
PORT=3000
LOG_LEVEL=info
DATABASE_URL=postgresql://user:password@localhost:5432/air360
```

`DATABASE_URL` is required. The server will not start without it.

## Migrations

Migration files live in `/backend/migrations/` and use
`YYYYMMDDHHmmss_<description>.sql` names. They are applied in alphabetical
order.

Each migration file contains DDL. The runner wraps each migration in a
transaction and records the version in `schema_migrations`.

```bash
npm run migrate
```

The migration command is safe to run repeatedly. Already applied migrations are
skipped.

## Data Model

```text
devices
  device_id          BIGINT PK       - device ID from firmware, 48-bit MAC as decimal
  public_id          UUID UNIQUE     - public identifier exposed on external APIs
  registered_from_ip TEXT NULL       - IP address seen during device registration
  name               TEXT            - device name from firmware config
  latitude           FLOAT8          - user-provided device latitude
  longitude          FLOAT8          - user-provided device longitude
  firmware_version   TEXT
  upload_secret_hash TEXT NULL       - sha256:<base64url(sha256(upload_secret))>
  last_batch_id      BIGINT NULL     - batch_id of the most recent ingest, updated on each ingest
  registered_at      TIMESTAMPTZ     - set once on first registration
  last_seen_at       TIMESTAMPTZ     - updated on registration and ingest

batches
  device_id          BIGINT PK       - references devices(device_id)
  batch_id           BIGINT PK       - assigned by firmware, used for idempotency
  received_at        TIMESTAMPTZ     - server time

measurements
  device_id          BIGINT          - with batch_id references batches
  batch_id           BIGINT          - with device_id references batches
  sensor_type        TEXT
  kind               TEXT
  value              DOUBLE PRECISION
  sampled_at         TIMESTAMPTZ     - timestamp from device sample_time_unix_ms
  received_at        TIMESTAMPTZ     - server time, for latency diagnostics
```

`measurements` is converted to a TimescaleDB hypertable partitioned by
`sampled_at` with 7-day chunks. Compression is enabled with `sampled_at DESC`
ordering and `device_id, sensor_type` segmenting.

`BIGINT` columns are parsed as JavaScript `number` by the pg driver, configured
globally in `/backend/src/db/client.ts`. Device IDs are 48-bit values and fit
within `Number.MAX_SAFE_INTEGER`.

`public_id` is a UUID generated on device registration. External-facing APIs use
`public_id` to identify devices; `device_id` is internal.

## API

All timestamps are UTC. All endpoints return JSON unless noted otherwise.

### `GET /`

Service liveness check.

Response `200`:

```json
{ "service": "air360-api-backend", "status": "ok" }
```

### `GET /health`

Health check.

Response `200`:

```json
{ "ok": true }
```

### `GET /v1/devices`

Returns all registered devices with their location and latest sensor readings.
Intended for the portal map page.

Response `200`:

```json
{
  "devices": [
    {
      "public_id": "550e8400-e29b-41d4-a716-446655440000",
      "name": "Air360-AB12",
      "location": { "latitude": 55.751244, "longitude": 37.618423 },
      "last_seen_at": "2026-04-29T10:00:00.000Z",
      "sensors": [
        {
          "sensor_type": "bme280",
          "readings": [
            { "kind": "temperature_c", "value": 22.5, "sampled_at": "2026-04-29T09:59:00.000Z" },
            { "kind": "humidity_percent", "value": 48.0, "sampled_at": "2026-04-29T09:59:00.000Z" }
          ]
        }
      ]
    }
  ]
}
```

Devices with no measurements appear with `sensors: []`.

---

### `GET /v1/devices/:public_id/measurements?period=<period>`

Returns time-bucketed sensor measurements for charts. Intended for the portal
device detail page.

Path parameters:

| Parameter | Type | Description |
|-----------|------|-------------|
| `public_id` | UUID | Public device identifier |

Query parameters:

| Parameter | Required | Values |
|-----------|----------|--------|
| `period` | yes | `1h` `24h` `7d` `30d` `90d` `180d` `365d` |

Bucket sizes and approximate point counts per series:

| `period` | Bucket | Points |
|----------|--------|--------|
| `1h` | 1 minute | ~60 |
| `24h` | 5 minutes | ~288 |
| `7d` | 1 hour | ~168 |
| `30d` | 6 hours | ~120 |
| `90d` | 1 day | ~90 |
| `180d` | 1 day | ~180 |
| `365d` | 1 day | ~365 |

Each point's value is the average (`AVG`) of all readings in that bucket.
Short keys `t` (timestamp) and `v` (value) reduce payload size.

Response `200`:

```json
{
  "public_id": "550e8400-e29b-41d4-a716-446655440000",
  "period": "24h",
  "sensors": [
    {
      "sensor_type": "bme280",
      "series": [
        {
          "kind": "temperature_c",
          "points": [
            { "t": "2026-04-28T10:00:00.000Z", "v": 22.1 },
            { "t": "2026-04-28T10:05:00.000Z", "v": 22.3 }
          ]
        }
      ]
    }
  ]
}
```

Error responses:

| Code | `error.code` | Reason |
|------|--------------|--------|
| 400 | `validation_error` | Missing or invalid `period` |
| 404 | `device_not_found` | Device does not exist |

---

### `PUT /v1/devices/:device_id/register`

Registers a device or updates its metadata. Firmware calls this on boot.
The endpoint is idempotent and safe to call multiple times.

Path parameters:

| Parameter | Type | Description |
|-----------|------|-------------|
| `device_id` | integer | Hardware identifier, 48-bit MAC as decimal |

Request body:

```json
{
  "schema_version": 1,
  "name": "Air360-AB12",
  "firmware_version": "1.2.0",
  "location": {
    "latitude": 55.751244,
    "longitude": 37.618423
  },
  "upload_secret_hash": "sha256:base64url-sha256-value"
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `name` | string | yes | Device name from firmware config |
| `firmware_version` | string | yes | Firmware version string |
| `location.latitude` | number | yes | Decimal degrees, -90 to 90 |
| `location.longitude` | number | yes | Decimal degrees, -180 to 180 |
| `upload_secret_hash` | string | yes | `sha256:<base64url(sha256(upload_secret))>` |
| `schema_version` | integer | no | Currently `1` |

Registration rules:

- A new `device_id` creates the device record and stores the hash.
- An existing `device_id` with the same hash updates name, location, firmware version, and `last_seen_at`.
- An existing `device_id` with a different hash is rejected with `401`.

Response `200`:

```json
{
  "schema_version": 1,
  "status": "registered",
  "public_id": "550e8400-e29b-41d4-a716-446655440000",
  "registered_at": "2026-04-27T09:15:00.000Z",
  "last_seen_at": "2026-04-27T09:15:00.000Z"
}
```

Error responses:

| Code | `error.code` | Reason |
|------|--------------|--------|
| 400 | `validation_error` | Missing or invalid field |
| 401 | `invalid_upload_secret` | Hash does not match the existing device record |
| 500 | `internal_error` | Unexpected server error |

### `PUT /v1/devices/:device_id/batches/:batch_id`

Ingests a batch of sensor samples from a device and persists them to the
database. Repeated calls with the same `batch_id` are accepted but not stored
twice.

Request headers:

| Header | Required | Description |
|--------|----------|-------------|
| `Authorization` | yes | `Bearer <upload_secret>` — the raw secret generated by firmware |

Path parameters:

| Parameter | Type | Description |
|-----------|------|-------------|
| `device_id` | integer | Device hardware identifier |
| `batch_id` | integer | Batch identifier assigned by firmware |

Request body:

```json
{
  "schema_version": 1,
  "sent_at_unix_ms": 1714220400000,
  "device": {
    "device_id": "281474976710655",
    "firmware_version": "1.2.0"
  },
  "batch": {
    "sample_count": 1,
    "samples": [
      {
        "sensor_type": "bme280",
        "sample_time_unix_ms": 1714220399000,
        "values": [
          { "kind": "temperature_c", "value": 22.5 },
          { "kind": "humidity_percent", "value": 48.0 },
          { "kind": "pressure_hpa", "value": 1013.2 }
        ]
      }
    ]
  }
}
```

Rules enforced by the current implementation:

- If present, `device.device_id` must match the `device_id` path parameter.
- `batch.samples` must be an array.
- `batch.sample_count` must equal `batch.samples.length`.
- Every sample must include numeric `sample_time_unix_ms`.
- Every `sample.sensor_type` must be supported.
- Every `values[].kind` must be supported.
- The device must already be registered.

Supported `sensor_type` values:

`bme280` `bme680` `sps30` `scd30` `veml7700` `gps_nmea` `dht11` `dht22` `htu2x` `sht4x` `ds18b20` `me3_no2` `ina219` `mhz19b`

Supported `values[].kind` values:

`temperature_c` `humidity_percent` `pressure_hpa` `latitude_deg` `longitude_deg` `altitude_m` `satellites` `speed_knots` `course_deg` `hdop` `gas_resistance_ohms` `pm1_0_ug_m3` `pm2_5_ug_m3` `pm4_0_ug_m3` `pm10_0_ug_m3` `nc0_5_per_cm3` `nc1_0_per_cm3` `nc2_5_per_cm3` `nc4_0_per_cm3` `nc10_0_per_cm3` `typical_particle_size_um` `co2_ppm` `illuminance_lux` `adc_raw` `voltage_mv` `current_ma` `power_mw`

Response `200`:

Empty body.

Error responses:

| Code | `error.code` | Reason |
|------|--------------|--------|
| 400 | `invalid_payload` | Validation failed |
| 401 | `invalid_upload_secret` | Bearer secret does not match the device |
| 404 | `device_not_found` | Device has not been registered |
| 500 | `internal_error` | Unexpected server error |

### `GET /v1/devices/:public_id/latest`

Returns the latest reading for each `(sensor_type, kind)` pair recorded by the
device.

Path parameters:

| Parameter | Type | Description |
|-----------|------|-------------|
| `public_id` | UUID | Public device identifier from the register response |

Response `200`:

```json
{
  "public_id": "550e8400-e29b-41d4-a716-446655440000",
  "last_seen_at": "2026-04-27T09:30:00.000Z",
  "sensors": [
    {
      "sensor_type": "bme280",
      "readings": [
        { "kind": "temperature_c", "value": 22.5, "sampled_at": "2026-04-27T09:29:55.000Z" },
        { "kind": "humidity_percent", "value": 48.0, "sampled_at": "2026-04-27T09:29:55.000Z" },
        { "kind": "pressure_hpa", "value": 1013.2, "sampled_at": "2026-04-27T09:29:55.000Z" }
      ]
    },
    {
      "sensor_type": "sps30",
      "readings": [
        { "kind": "pm2_5_ug_m3", "value": 12.3, "sampled_at": "2026-04-27T09:29:55.000Z" }
      ]
    }
  ]
}
```

If the device has no measurements yet, `sensors` is an empty array.

Error responses:

| Code | `error.code` | Reason |
|------|--------------|--------|
| 404 | `device_not_found` | Device has not been registered |
| 500 | `internal_error` | Unexpected server error |

## Current State

- Device registration and upsert: implemented.
- Sensor batch ingest with persistence: implemented.
- Latest readings per device: implemented.
- TimescaleDB hypertable and compression migrations: implemented.
- Upload secret auth (bearer token on ingest, hash stored at registration): implemented.
- Device list with latest measurements (`GET /v1/devices`): implemented.
- Time-bucketed measurement history (`GET /v1/devices/:public_id/measurements`): implemented.
