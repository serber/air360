# Portal — Map and Device Pages ADR

## Status

Proposed.

## Decision Summary

Build the Air360 portal as a Next.js 16 application with two public pages: a
map page showing all devices as pins with a latest-readings popup, and a device
detail page with sensor charts for selectable time periods. All data is public
— no authentication is required.

## Context

The Air360 backend exposes device location and latest measurements through its
REST API. The portal consumes this API to provide a read-only public interface
for visualizing device data.

## Goals

- Show all registered devices on a world map with location pins.
- Display latest sensor readings for a device without leaving the map.
- Provide a dedicated device page with time-series charts for sensor data.
- Keep the dependency footprint minimal.

## Non-Goals

- Authentication or user accounts.
- Real-time data updates (polling or WebSocket).
- Admin or device management.
- Bounding-box or geographic filtering (deferred).

## Architectural Decision

### Pages

| Route | Purpose |
|-------|---------|
| `/` | Placeholder public portal home page |
| `/map` | World map with all device pins |
| `/devices/:public_id` | Device detail page with sensor charts |

### Map (`/map`)

**Library:** `react-leaflet` + OpenStreetMap tiles — consistent with the
firmware embedded web UI.

Each device is rendered as a map marker at its stored `latitude`/`longitude`.
Clicking a marker opens a Leaflet popup containing:

- Device name
- `last_seen_at` timestamp
- Latest reading per `(sensor_type, kind)`, grouped by sensor type
- Link to the device detail page

Data is fetched client-side on mount from `GET /v1/devices`.

### Device detail page (`/devices/:public_id`)

**Library:** Recharts — the most widely-used React-native charting library,
with full TypeScript support, `ResponsiveContainer`, and composable `LineChart`
primitives suited to time-series sensor data.

The page shows one chart per `sensor_type`. Each chart contains one line per
measurement `kind` (e.g., `temperature_c` and `humidity_percent` on the same
climate chart).

A period selector at the top of the page controls the time range:

| Label | `period` value |
|-------|----------------|
| 1 hour | `1h` |
| 24 hours | `24h` |
| 7 days | `7d` |
| 30 days | `30d` |
| 3 months | `90d` |
| 6 months | `180d` |
| 1 year | `365d` |

Selecting a period fetches
`GET /v1/devices/:public_id/measurements?period=<value>`.

### Data fetching

Both pages use native `fetch` inside React client components. No additional
data-fetching library is introduced. The map component and device charts are
client components (`"use client"`) because `react-leaflet` and Recharts require
browser APIs.

### Component layout

| File | Purpose |
|------|---------|
| `src/app/page.tsx` | Placeholder home page |
| `src/app/map/page.tsx` | Map page (shell, imports `DeviceMap`) |
| `src/app/devices/[public_id]/page.tsx` | Device detail page shell |
| `src/components/DeviceMap.tsx` | `react-leaflet` map with device markers |
| `src/components/DevicePopup.tsx` | Popup card: name, last seen, readings, link |
| `src/components/SensorChart.tsx` | Recharts `LineChart` wrapper per sensor type |
| `src/components/PeriodSelector.tsx` | Period toggle buttons |

### Dependencies to add

| Package | Purpose |
|---------|---------|
| `leaflet` | Map engine |
| `react-leaflet` | React bindings for Leaflet |
| `@types/leaflet` | TypeScript types |
| `recharts` | Time-series charts |

## Alternatives Considered

### Mapbox / Google Maps

Higher fidelity but require API keys and have usage-based pricing. OpenStreetMap
is free and already used in the firmware web UI.

### Nivo

More advanced chart types but heavier bundle. Recharts is sufficient for
line charts and has a larger ecosystem.

### SWR / React Query

Adds caching and revalidation. Deferred — native `fetch` is sufficient while
there is no auto-refresh requirement.
