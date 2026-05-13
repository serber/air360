# Portal API Extensions ADR

## Status

Accepted. Implemented in `backend/`.

## Decision Summary

Add portal read endpoints to the Air360 API backend: active and offline device
lists for the map, public home-page summary counters, plus a measurements
history endpoint returning device metadata, latest readings, and time-bucketed
sensor data for a given device and period.

## Context

The portal map page needs all devices at once with their latest readings. The
device detail page needs time-series data for selectable periods. Neither
endpoint currently exists.

## Goals

- Expose all devices with location and latest readings in one request.
- Expose offline devices separately without stale latest readings.
- Expose summary counters for the portal home page.
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

Returns devices seen within the last hour with their latest measurements.

Response `200`:

```json
{
  "devices": [
    {
      "public_id": "550e8400-e29b-41d4-a716-446655440000",
      "name": "Air360-AB12",
      "geo_country_code": "RU",
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

### `GET /v1/devices/offline`

Returns devices that have not been seen for more than one hour. The endpoint is
used by the portal offline-device layer and returns `sensors: []` so stale
readings are not displayed as current measurements.

Response `200`:

```json
{
  "devices": [
    {
      "public_id": "550e8400-e29b-41d4-a716-446655440000",
      "name": "Air360-AB12",
      "geo_country_code": "RU",
      "location": {
        "latitude": 55.751244,
        "longitude": 37.618423
      },
      "last_seen_at": "2026-04-29T08:00:00.000Z",
      "sensors": []
    }
  ]
}
```

### `GET /v1/stats`

Returns public summary counters for the portal home page.

Response `200`:

```json
{
  "active_devices": 42,
  "countries": 7,
  "reports_24h": 1440
}
```

`active_devices` uses the same one-hour freshness window as `GET /v1/devices`.
`countries` counts distinct non-empty `geo_country_code` values across all
devices. `reports_24h` counts batch rows received in the last 24 hours.

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
  "device": {
    "name": "Air360-AB12",
    "latitude": 55.751244,
    "longitude": 37.618423,
    "altitude_m": 156.0,
    "firmware_version": "1.2.0",
    "registered_at": "2026-04-27T09:15:00.000Z",
    "last_seen_at": "2026-04-29T10:00:00.000Z",
    "geo_country": "Russia",
    "geo_country_code": "ru",
    "geo_city": "Moscow"
  },
  "by_kind": [
    {
      "kind": "temperature_c",
      "series": [
        {
          "sensor_type": "bme280",
          "points": [
            { "t": "2026-04-28T10:00:00.000Z", "v": 22.1 },
            { "t": "2026-04-28T10:05:00.000Z", "v": 22.3 }
          ]
        }
      ]
    }
  ],
  "latest": [
    {
      "sensor_type": "bme280",
      "kind": "temperature_c",
      "value": 22.5,
      "sampled_at": "2026-04-29T09:59:00.000Z"
    }
  ],
  "sensors": [
    {
      "sensor_type": "bme280",
      "kinds": ["temperature_c"]
    }
  ]
}
```

GPS measurements are intentionally excluded from chart series and latest-reading
metadata on this endpoint. The device object carries the current coordinates and
reverse-geocoded display fields instead.

Error responses:

| HTTP | `error.code` | Meaning |
|------|--------------|---------|
| 400 | `validation_error` | Invalid or missing `period` |
| 404 | `device_not_found` | Device does not exist |

### Affected backend files

- `backend/src/routes/v1/devices.ts` — add `GET /v1/devices` and `GET /v1/devices/offline` handlers
- `backend/src/routes/v1/stats.ts` — add `GET /v1/stats` handler
- `backend/src/routes/v1/measurements.ts` — new route file for `GET /v1/devices/:public_id/measurements`
- `backend/src/routes/v1/index.ts` — register measurements routes
- `backend/src/modules/devices/device-repository.ts` — add active and offline device list queries
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
