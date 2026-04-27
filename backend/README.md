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
  routes/                — route registration
    v1/
      devices.ts         — device registration
      ingest.ts          — sensor batch ingest
  modules/
    devices/
      device-repository.ts — device DB operations
  db/
    client.ts            — Kysely singleton
    schema.ts            — TypeScript types for DB tables
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

Migration files live in `migrations/` and are applied in alphabetical order. Each migration is wrapped in a transaction. Applied versions are tracked in the `schema_migrations` table.

```bash
npm run migrate
```

Safe to run repeatedly — already applied migrations are skipped.

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

| Parameter | Type   | Description                        |
|-----------|--------|------------------------------------|
| `chip_id` | string | Unique hardware identifier (ESP32) |

**Request body**

```json
{
  "name": "Air360-AB12",
  "latitude": 55.751244,
  "longitude": 37.618423,
  "firmware_version": "1.2.0"
}
```

| Field              | Type   | Required | Description                          |
|--------------------|--------|----------|--------------------------------------|
| `name`             | string | yes      | Device name from firmware config     |
| `latitude`         | number | yes      | Decimal degrees, −90 to 90           |
| `longitude`        | number | yes      | Decimal degrees, −180 to 180         |
| `firmware_version` | string | yes      | Firmware version string              |

**Response `200`** — created or updated

```json
{
  "id": "019680ab-1234-7abc-8def-000000000001",
  "chip_id": "AB1234CDEF56",
  "name": "Air360-AB12",
  "latitude": 55.751244,
  "longitude": 37.618423,
  "firmware_version": "1.2.0",
  "registered_at": "2026-04-27T12:00:00.000Z",
  "last_seen_at": "2026-04-27T15:30:00.000Z"
}
```

On first call: `registered_at` and `last_seen_at` are set to current time.  
On subsequent calls: `name`, `latitude`, `longitude`, `firmware_version`, and `last_seen_at` are updated. `registered_at` is never changed.

**Error responses**

| Code | `error.code`       | Reason                              |
|------|--------------------|-------------------------------------|
| 400  | `validation_error` | Missing or invalid field            |
| 500  | `internal_error`   | Unexpected server error             |

---

### `PUT /v1/devices/:chip_id/batches/:client_batch_id`

Ingests a batch of sensor samples from a device. Currently validates payload and returns a mock accepted response — persistence is not yet implemented.

**Path parameters**

| Parameter         | Type   | Description                              |
|-------------------|--------|------------------------------------------|
| `chip_id`         | string | Device hardware identifier               |
| `client_batch_id` | string | Client-generated batch identifier        |

**Request body**

```json
{
  "schema_version": 1,
  "sent_at_unix_ms": 1714220400000,
  "device": {
    "chip_id": "AB1234CDEF56",
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

**Response `201`**

```json
{
  "accepted": true,
  "client_batch_id": "batch-001",
  "accepted_samples": 1
}
```

**Error responses**

| Code | `error.code`       | Reason                                      |
|------|--------------------|---------------------------------------------|
| 400  | `invalid_payload`  | Validation failed (see message for details) |
| 500  | `internal_error`   | Unexpected server error                     |

## Current state

- Device registration and upsert — implemented
- Sensor batch ingest — payload validation only, no persistence yet
- Auth — not implemented (public API by design)
