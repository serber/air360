# Portal API Extensions ADR

## Status

Accepted. Implemented in `backend/`.

## Decision Summary

Add two read endpoints to the Air360 API backend to support the portal: a
device list endpoint returning all devices with their latest measurements, and a
measurements history endpoint returning time-bucketed sensor data for a given
device and period.

## Context

The portal map page needs all devices at once with their latest readings. The
device detail page needs time-series data for selectable periods. Neither
endpoint currently exists.

## Goals

- Expose all devices with location and latest readings in one request.
- Expose time-bucketed historical measurements for a device and period.
- Keep the response shape simple and portal-driven.
- Use TimescaleDB `time_bucket()` for efficient aggregation.

## Non-Goals

- Pagination of the device list (acceptable for current device count).
- Authentication.
- Bounding-box or geographic device filtering (deferred).
- Custom `from`/`to` range queries (period enum is sufficient for the initial UI).

## Architectural Decision

### `GET /v1/devices`

Returns all registered devices with their latest measurements.

Response `200`:

```json
{
  "devices": [
    {
      "public_id": "550e8400-e29b-41d4-a716-446655440000",
      "name": "Air360-AB12",
      "location": {
        "latitude": 55.751244,
        "longitude": 37.618423
      },
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

### `GET /v1/devices/:public_id/measurements?period=<period>`

Returns time-bucketed measurements suitable for charts.

**`period` values and aggregation:**

| `period` | Bucket size | Approx. data points per series |
|----------|-------------|-------------------------------|
| `1h` | 1 minute | ~60 |
| `24h` | 5 minutes | ~288 |
| `7d` | 1 hour | ~168 |
| `30d` | 6 hours | ~120 |
| `90d` | 1 day | ~90 |
| `180d` | 1 day | ~180 |
| `365d` | 1 day | ~365 |

The aggregated value per bucket is `AVG` of all readings in that window.
TimescaleDB `time_bucket()` is used for aggregation.

Short keys `t` (timestamp) and `v` (value) reduce payload size on large ranges.

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
        },
        {
          "kind": "humidity_percent",
          "points": [
            { "t": "2026-04-28T10:00:00.000Z", "v": 47.8 },
            { "t": "2026-04-28T10:05:00.000Z", "v": 48.1 }
          ]
        }
      ]
    }
  ]
}
```

Error responses:

| HTTP | `error.code` | Meaning |
|------|--------------|---------|
| 400 | `validation_error` | Invalid or missing `period` |
| 404 | `device_not_found` | Device does not exist |

### Affected backend files

- `backend/src/routes/v1/devices.ts` — add `GET /v1/devices` handler
- `backend/src/routes/v1/measurements.ts` — new route file for `GET /v1/devices/:public_id/measurements`
- `backend/src/routes/v1/index.ts` — register measurements routes
- `backend/src/modules/devices/device-repository.ts` — add `findAllDevices()`
- `backend/src/modules/measurements/measurement-repository.ts` — add `findMeasurementSeries()`
- `docs/backend/README.md` — update API reference after implementation

## Alternatives Considered

### Single combined endpoint

Return history and latest in one call. Rejected — the map page only needs
latest readings and adding history would make the map payload unnecessarily
large.

### `from` / `to` query parameters instead of `period`

More flexible but requires the UI to manage timestamps. The `period` enum maps
directly to the UI buttons and simplifies both the client and the server.
Period-to-bucket mapping lives in one place on the server.
