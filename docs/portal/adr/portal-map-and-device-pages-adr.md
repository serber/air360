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

**Library:** `maplibre-gl` + OpenStreetMap raster tiles.

Each device is rendered as a GeoJSON point at its stored `latitude`/`longitude`.
Clicking a point opens a MapLibre popup containing:

- Device name
- `last_seen_at` timestamp
- Latest reading per `(sensor_type, kind)`, grouped by sensor type
- Link to the device detail page

The map exposes a metric selector for humidity, pressure, temperature, CO2,
PM10, PM1.0, PM2.5, PM4.0, and illuminance. The selected
metric controls marker fill color using simple threshold bands where the value
has a meaningful quality interpretation. Temperature uses a separate cold-to-hot
scale from blue through teal/green/yellow to red, with granular bands for below
-20 C, -20..-10 C, -10..0 C, 0..10 C, 10..18 C, 18..24 C, 24..28 C,
28..32 C, 32..40 C, and above 40 C. Pressure uses a separate low-to-high
atmospheric pressure scale: below 980 hPa as very low, 980-1000 hPa as low,
1000-1010 hPa as slightly low, 1010-1020 hPa as normal, 1020-1030 hPa as
slightly high, 1030-1040 hPa as high, and above 1040 hPa as very high. Humidity
uses a dry-to-humid relative humidity scale: below 20% as very dry, 20-30% as
dry, 30-60% as normal, 60-75% as humid, 75-90% as very humid, and above 90% as
saturated or fog-like. CO2 uses a baseline-to-high scale where values below
380 ppm indicate a likely calibration/data issue, 380-450 ppm is treated as low
or normal outdoor background, 450-600 ppm as slightly elevated, 600-800 ppm as
elevated, 800-1000 ppm as high, and above 1000 ppm as very high. PM2.5 uses an
excellent-to-hazardous particulate matter scale: 5 ug/m3 and below as excellent,
5-9 ug/m3 as good, 9.1-15 ug/m3 as moderate, 15.1-35.4 ug/m3 as elevated,
35.5-55.4 ug/m3 as unhealthy for sensitive groups, 55.5-125.4 ug/m3 as
unhealthy, 125.5-225.4 ug/m3 as very unhealthy, and above 225.5 ug/m3 as
hazardous. PM10 uses the same excellent-to-hazardous category model: 15 ug/m3
and below as excellent, 15-45 ug/m3 as good, 45-54 ug/m3 as moderate,
55-154 ug/m3 as elevated, 155-254 ug/m3 as unhealthy for sensitive groups,
255-354 ug/m3 as unhealthy, 355-424 ug/m3 as very unhealthy, and above
425 ug/m3 as hazardous. PM1.0 uses the same category direction with tighter
bands: 3 ug/m3 and below as excellent, 3-6 ug/m3 as good, 6-10 ug/m3 as
moderate, 10-20 ug/m3 as elevated, 20-35 ug/m3 as high, 35-55 ug/m3 as very
high, and above 55 ug/m3 as hazardous. PM4.0 uses the same category direction:
8 ug/m3 and below as excellent, 8-15 ug/m3 as good, 15-25 ug/m3 as moderate,
25-50 ug/m3 as elevated, 50-100 ug/m3 as high, 100-200 ug/m3 as very high, and
above 200 ug/m3 as hazardous. Typical particle size remains available in device
readings, but is intentionally not exposed as a map layer because it is
descriptive rather than a good/bad quality metric. Illuminance is also
descriptive rather than good/bad: below 10 lux is dark, 10-200 lux is dim light,
200-1000 lux is bright indoor light, 1000-10000 lux is daylight, and above
10000 lux is bright sun.
Data-only metrics use a neutral marker color. Device freshness from
`last_seen_at` is encoded separately with marker opacity and border style so
stale devices remain visible without consuming the selected metric color
channel. Dense device areas use compact clusters with capped visual size;
cluster labels show the average value for the selected metric, cluster fill
color is based on that average value, and individual circular markers scale with
map zoom. Map status, the selected metric legend, and the metric selector are
placed in left-side overlays.

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
client components (`"use client"`) because MapLibre GL JS and Recharts require
browser APIs.

### Component layout

| File | Purpose |
|------|---------|
| `src/app/page.tsx` | Placeholder home page |
| `src/app/map/page.tsx` | Map page (shell, imports `DeviceMap`) |
| `src/app/devices/[public_id]/page.tsx` | Device detail page shell |
| `src/components/DeviceMap.tsx` | MapLibre map with GeoJSON device layers |
| `src/components/DevicePopup.tsx` | Popup card: name, last seen, readings, link |
| `src/components/SensorChart.tsx` | Recharts `LineChart` wrapper per sensor type |
| `src/components/PeriodSelector.tsx` | Period toggle buttons |

### Dependencies to add

| Package | Purpose |
|---------|---------|
| `maplibre-gl` | WebGL map engine and GeoJSON device layers |
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
