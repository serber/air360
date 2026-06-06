# Air360 Portal Docs

## Scope

This directory captures the working direction and implemented behavior for the
Air360 public portal.

The portal is a separate web application. It is not part of `firmware/`, and it
is not the same application as `backend/`.

The `portal/` source tree is a standalone `Next.js` application. This document
is the high-level map; implementation details should be verified against the
actual `portal/` codebase.

## Related Documents

| Document | Purpose |
|----------|---------|
| [../../portal/AGENTS.md](../../portal/AGENTS.md) | Portal-local working contract for AI agents |
| [../../portal/README.md](../../portal/README.md) | Source directory entry point and commands |
| [adr/README.md](adr/README.md) | Portal architecture decision records |
| [ubuntu-deployment.md](ubuntu-deployment.md) | Ubuntu deployment guide |

## Portal Role

The portal currently covers public read-only project and device pages:

- public home page at `/`
- map page at `/map` with active devices from `GET /v1/devices` and optional
  offline devices from `GET /v1/devices/offline`
- build guide page at `/build` with shield-board and direct ESP32-S3 wiring
  paths, including default firmware pin assignments for direct sensor wiring
- privacy page at `/privacy` using the shared portal shell and document layout
- device popup with latest readings grouped by sensor type
- device popup shows a country flag when `geo_country_code` is present in the
  public device summary response
- map metric selector for humidity, pressure, temperature, CO2, PM10, PM1.0,
  PM2.5, PM4.0, and illuminance
- marker quality color based on the selected metric, with opacity and border
  style indicating device freshness by `last_seen_at`
- temperature markers use a dedicated cold-to-hot color scale from blue through
  teal/green/yellow to red, including clustered average values and granular
  temperature bands from below -20 C through above 40 C
- pressure markers use a dedicated low-to-high atmospheric pressure color scale,
  centered on the normal sea-level range around 1010-1020 hPa, with bands from
  below 980 hPa through above 1040 hPa
- humidity markers use a dedicated dry-to-humid relative humidity color scale,
  with the normal range around 30-60% shown in green and saturated/fog-like
  values above 90% shown separately
- CO2 markers use a dedicated fresh-to-very-high color scale, with practical
  outdoor background and elevated indoor ranges from below 380 ppm through
  above 1000 ppm
- PM2.5 markers use a dedicated excellent-to-hazardous particulate matter scale
  from 5 ug/m3 and below through above 225.5 ug/m3
- PM10 markers use a dedicated excellent-to-hazardous particulate matter scale
  from 15 ug/m3 and below through above 425 ug/m3
- PM1.0 markers use a dedicated excellent-to-hazardous particulate matter scale
  from 3 ug/m3 and below through above 55 ug/m3
- PM4.0 markers use a dedicated excellent-to-hazardous particulate matter scale
  from 8 ug/m3 and below through above 200 ug/m3
- illuminance markers use a descriptive dark-to-bright scale from below 10 lux
  through above 10000 lux instead of good/bad quality categories
- compact clusters for dense areas, with capped cluster sizes and circular
  marker sizes changing by map zoom level
- cluster labels show the average value for the currently selected metric
- marker and cluster labels round displayed measurement values to the nearest
  tenth
- map status, metric legend, and metric selector are placed in left-side
  overlays
- the map page uses the shared portal shell and an Air360 layer-chip control for
  measurement selection while keeping the implemented metric legends and
  freshness indicators unchanged
- device detail page at `/devices/:public_id` with sensor charts from
  `GET /v1/devices/:public_id/measurements?period=<period>`
- latest-reading cards, sensor metadata, device coordinates, and reverse-geocoded
  display labels from the same measurement response
- device detail pages use the shared portal shell, a current-reading strip,
  segmented period controls, chart cards, and a right-side metadata/sidebar
  layout
- shared Air360 visual tokens and shell primitives for the public portal home
  page, including reusable navigation, footer, buttons, cards, metric grids, and
  live-data preview blocks
- home-page summary counters loaded from `GET /v1/stats` instead of hard-coded
  values
- public `/build` page that starts the user-facing device assembly guide with
  two paths: using the Air360 shield board or wiring sensors directly to
  ESP32-S3 pins

Potential future account flows remain out of scope for the current portal
implementation.

## Working Stack Direction

The current working direction for the portal is:

- `Next.js` as the frontend application framework
- `React` as the UI layer
- `TypeScript` for application code
- `next-intl` for UI localization

`Next.js` is a good fit here because the portal needs public pages, client-side
map rendering, and device detail pages with browser-only charting libraries.

The map uses `maplibre-gl` with OpenStreetMap raster tiles and GeoJSON-backed
device layers. The device detail page uses `recharts` for time-series charts.
UI strings are loaded through `next-intl`; the current locales are `en` and
`ru`, with messages stored in `portal/messages/en.json` and
`portal/messages/ru.json`. The header language toggle stores the selected
locale in the `air360-locale` cookie. If the cookie is absent, the portal uses
the browser `Accept-Language` header to choose between `en` and `ru`, falling
back to `en`. Exact choices for future form libraries and auth helpers remain
open and should be selected later to match the actual backend auth model.

## Boundary Between Portal And Backend

The portal should remain a separate application from `backend/`.

Responsibility split:

- `portal`
  - page rendering
  - navigation
  - forms for registration and sign-in
  - personal account UI
  - device management UI
- `backend`
  - API endpoints
  - user and device data
  - authorization rules
  - session or token handling
  - business logic and persistence

The backend source tree in `/backend` remains the source of truth for implemented API behavior.

## Backend Communication

The portal is expected to communicate with the backend over HTTP API.

Current public integration areas are:

- `GET /v1/devices`
- `GET /v1/devices/offline`
- `GET /v1/devices/:public_id/measurements?period=<period>`

The portal should not communicate directly with firmware devices for account workflows. Those interactions should go through the backend API layer.

Based on the current backend structure, versioned API routes under `/v1` are the expected integration direction, but the exact contract should follow the implemented backend endpoints.

In local and production Next.js runtime, `next.config.ts` rewrites `/v1/*` to
`AIR360_API_BASE_URL` and defaults to `https://api.air360.ru`. Browser-side
direct API calls can be forced with `NEXT_PUBLIC_AIR360_API_BASE_URL`, but the
default portal flow uses same-origin `/v1/*` requests.

## Current Status

- `backend/` already exists as a separate Fastify service
- `firmware/` already exists as the device-side implementation
- `portal/` exists as a `Next.js` project with a public home page, public map,
  build guide, privacy page, and device pages
- the public home, map, device detail, and privacy pages use reusable `air-*`
  global style primitives and shared shell primitives from
  `portal/src/components/PortalShell.tsx`
- Ubuntu run and deployment instructions live in `docs/portal/ubuntu-deployment.md`

This document records the working scope and boundary for the current portal.
