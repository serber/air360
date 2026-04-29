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
  latitude           NUMERIC(9,6)    - user-provided device latitude
  longitude          NUMERIC(9,6)    - user-provided device longitude
  firmware_version   TEXT
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

### `PUT /v1/devices/:device_id/register`

Registers a device or updates its metadata. Firmware calls this on boot.
The endpoint is idempotent and safe to call multiple times.

Planned contract change: [adr/firmware-generated-upload-secret-adr.md](adr/firmware-generated-upload-secret-adr.md)
changes registration to use nested `location` and `upload_secret_hash`.

Path parameters:

| Parameter | Type | Description |
|-----------|------|-------------|
| `device_id` | integer | Hardware identifier, 48-bit MAC as decimal |

Request body:

```json
{
  "name": "Air360-AB12",
  "latitude": 55.751244,
  "longitude": 37.618423,
  "firmware_version": "1.2.0"
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `name` | string | yes | Device name from firmware config |
| `latitude` | number | yes | Decimal degrees, -90 to 90 |
| `longitude` | number | yes | Decimal degrees, -180 to 180 |
| `firmware_version` | string | yes | Firmware version string |

Response `200`:

```json
{
  "device_id": 281474976710655,
  "public_id": "550e8400-e29b-41d4-a716-446655440000",
  "registered_from_ip": "203.0.113.10",
  "name": "Air360-AB12",
  "latitude": 55.751244,
  "longitude": 37.618423,
  "firmware_version": "1.2.0",
  "registered_at": "2026-04-27T09:15:00.000Z",
  "last_seen_at": "2026-04-27T09:15:00.000Z"
}
```

On first call, `registered_at` and `last_seen_at` are set to current time. On
subsequent calls, `name`, `latitude`, `longitude`, `firmware_version`,
`registered_from_ip`, and `last_seen_at` are updated. `registered_at` is not
changed.

Error responses:

| Code | `error.code` | Reason |
|------|--------------|--------|
| 400 | `validation_error` | Missing or invalid field |
| 500 | `internal_error` | Unexpected server error |

### `PUT /v1/devices/:device_id/batches/:batch_id`

Ingests a batch of sensor samples from a device and persists them to the
database. Repeated calls with the same `batch_id` are accepted but not stored
twice.

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
- Auth: not implemented.
