# Air360 API Backend

Fastify-based API backend for Air360 device ingest and data storage.

## Stack

- **Runtime**: Node.js ≥ 20
- **Framework**: Fastify 5
- **Language**: TypeScript
- **Database**: PostgreSQL
- **Query builder**: Kysely
- **Driver**: pg (node-postgres)

## Structure

```
src/
  server.ts              — entry point, starts HTTP server
  app.ts                 — builds Fastify instance, registers config decorator
  config/env.ts          — environment variable parsing
  plugins/               — shared Fastify setup (error handler)
  routes/
    v1/
      devices.ts         — device registration and latest readings
      ingest.ts          — sensor batch ingest
  modules/
    devices/
      device-repository.ts     — device DB operations
    ingest/
      ingest-repository.ts     — batch and measurement DB operations
    measurements/
      measurement-repository.ts — measurement read queries
  db/
    client.ts            — Kysely singleton (BIGINT parsed as number)
    schema.ts            — TypeScript types for DB tables
  contracts/
    sensor-type.ts       — supported sensor types
    measurement-kind.ts  — supported measurement kinds
migrations/              — plain SQL migration files
scripts/
  migrate.ts             — migration runner
```

## Commands

```bash
npm install
npm run dev        # development server with hot reload
npm run build      # compile to dist/
npm start          # run compiled build
npm run typecheck  # type check without emitting
npm run migrate    # apply pending DB migrations
```

## Environment

Copy `.env.example` to `.env`:

```
HOST=0.0.0.0
PORT=3000
LOG_LEVEL=info
DATABASE_URL=postgresql://user:password@localhost:5432/air360
```

`DATABASE_URL` is required — the server will not start without it.

## Migrations

Migration files live in `migrations/` named `YYYYMMDDHHmmss_<description>.sql` and are applied in alphabetical order. Each file contains only DDL — the runner wraps each migration in a transaction and records the version in `schema_migrations` automatically.

```bash
npm run migrate
```

Safe to run repeatedly — already applied migrations are skipped.

## Data model

```
devices
  device_id        BIGINT PK         — chip ID from firmware (48-bit MAC as decimal)
  name             TEXT              — device name from firmware config
  latitude         NUMERIC(9,6)      — set by user when enabling Air360 API
  longitude        NUMERIC(9,6)
  firmware_version TEXT
  registered_at    TIMESTAMPTZ       — set once on first registration
  last_seen_at     TIMESTAMPTZ       — updated on every ingest batch

batches
  device_id        BIGINT  PK  → devices.device_id
  batch_id         BIGINT  PK        — assigned by firmware, used for idempotency
  received_at      TIMESTAMPTZ       — server time

measurements                    — append-only log, no primary key
  device_id        BIGINT  → batches(device_id, batch_id)
  batch_id         BIGINT  → batches(device_id, batch_id)
  sensor_type      TEXT
  kind             TEXT
  value            DOUBLE PRECISION
  sampled_at       TIMESTAMPTZ       — timestamp from device (sample_time_unix_ms)
  received_at      TIMESTAMPTZ       — server time, for latency diagnostics
```

`BIGINT` columns are parsed as JavaScript `number` by the pg driver (configured globally in `db/client.ts`). Device IDs are 48-bit values (max 2^48 − 1), safely within `Number.MAX_SAFE_INTEGER`.

## API

All timestamps are in UTC. All endpoints return JSON.

---

### `GET /`

Service liveness check.

**Response `200`**
```json
{ "service": "air360-api-backend", "status": "ok" }
```

---

### `GET /health`

Health check.

**Response `200`**
```json
{ "ok": true }
```

---

### `PUT /v1/devices/:chip_id/register`

Registers a device or updates its metadata. Called by firmware on every boot. Idempotent — safe to call multiple times.

**Path parameters**

| Parameter | Type    | Description                                     |
|-----------|---------|-------------------------------------------------|
| `chip_id` | integer | Hardware identifier — 48-bit MAC as decimal     |

**Request body**

```json
{
  "name": "Air360-AB12",
  "latitude": 55.751244,
  "longitude": 37.618423,
  "firmware_version": "1.2.0"
}
```

| Field              | Type   | Required | Description                      |
|--------------------|--------|----------|----------------------------------|
| `name`             | string | yes      | Device name from firmware config |
| `latitude`         | number | yes      | Decimal degrees, −90 to 90       |
| `longitude`        | number | yes      | Decimal degrees, −180 to 180     |
| `firmware_version` | string | yes      | Firmware version string          |

**Response `200`** — created or updated

```json
{
  "device_id": 281474976710655,
  "name": "Air360-AB12",
  "latitude": 55.751244,
  "longitude": 37.618423,
  "firmware_version": "1.2.0",
  "registered_at": "2026-04-27T09:15:00.000Z",
  "last_seen_at": "2026-04-27T09:15:00.000Z"
}
```

On first call: `registered_at` and `last_seen_at` are set to current time.  
On subsequent calls: `name`, `latitude`, `longitude`, `firmware_version`, and `last_seen_at` are updated. `registered_at` is never changed.

**Error responses**

| Code | `error.code`       | Reason                   |
|------|--------------------|--------------------------|
| 400  | `validation_error` | Missing or invalid field |
| 500  | `internal_error`   | Unexpected server error  |

---

### `PUT /v1/devices/:chip_id/batches/:batch_id`

Ingests a batch of sensor samples from a device and persists them to the database. Idempotent — repeated calls with the same `batch_id` are accepted but not stored twice.

**Path parameters**

| Parameter  | Type    | Description                              |
|------------|---------|------------------------------------------|
| `chip_id`  | integer | Device hardware identifier               |
| `batch_id` | integer | Batch identifier assigned by firmware    |

**Request body**

```json
{
  "schema_version": 1,
  "sent_at_unix_ms": 1714220400000,
  "device": {
    "chip_id": "281474976710655",
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

**Supported `sensor_type` values**

`bme280` `bme680` `sps30` `scd30` `veml7700` `gps_nmea` `dht11` `dht22` `htu2x` `sht4x` `ds18b20` `me3_no2` `ina219` `mhz19b`

**Supported `values[].kind` values**

`temperature_c` `humidity_percent` `pressure_hpa` `latitude_deg` `longitude_deg` `altitude_m` `satellites` `speed_knots` `course_deg` `hdop` `gas_resistance_ohms` `pm1_0_ug_m3` `pm2_5_ug_m3` `pm4_0_ug_m3` `pm10_0_ug_m3` `nc0_5_per_cm3` `nc1_0_per_cm3` `nc2_5_per_cm3` `nc4_0_per_cm3` `nc10_0_per_cm3` `typical_particle_size_um` `co2_ppm` `illuminance_lux` `adc_raw` `voltage_mv` `current_ma` `power_mw`

**Response `200`**

Empty body.

**Error responses**

| Code | `error.code`       | Reason                                      |
|------|--------------------|---------------------------------------------|
| 400  | `invalid_payload`  | Validation failed (see message for details) |
| 404  | `device_not_found` | Device has not been registered              |
| 500  | `internal_error`   | Unexpected server error                     |

---

### `GET /v1/devices/:chip_id/latest`

Returns the latest reading for each `(sensor_type, kind)` pair recorded by the device.

**Path parameters**

| Parameter | Type    | Description                |
|-----------|---------|----------------------------|
| `chip_id` | integer | Device hardware identifier |

**Response `200`**

```json
{
  "device_id": 281474976710655,
  "last_seen_at": "2026-04-27T09:30:00.000Z",
  "sensors": [
    {
      "sensor_type": "bme280",
      "readings": [
        { "kind": "temperature_c",    "value": 22.5,   "sampled_at": "2026-04-27T09:29:55.000Z" },
        { "kind": "humidity_percent", "value": 48.0,   "sampled_at": "2026-04-27T09:29:55.000Z" },
        { "kind": "pressure_hpa",    "value": 1013.2, "sampled_at": "2026-04-27T09:29:55.000Z" }
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

**Error responses**

| Code | `error.code`       | Reason                         |
|------|--------------------|--------------------------------|
| 404  | `device_not_found` | Device has not been registered |
| 500  | `internal_error`   | Unexpected server error        |

## Current state

- Device registration and upsert — implemented
- Sensor batch ingest with persistence — implemented
- Latest readings per device — implemented
- Auth — not implemented (public API by design)
