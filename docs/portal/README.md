# Air360 Portal Docs

## Scope

This directory captures the working direction for the future Air360 user portal.

The portal is planned as a separate web application. It is not part of `firmware/`, and it is not the same application as `backend/`.

The `portal/` source tree now exists as a standalone `Next.js` application scaffold. This document remains the high-level planning note, while implementation details should be verified against the actual `portal/` codebase.

## Planned Portal Role

The portal is intended to cover two main areas:

- a public home page that explains the Air360 project and acts as a landing page
- a private user area for account access and device management

The first implemented public flows are:

- placeholder home page at `/`
- map page at `/map` with all registered devices from `GET /v1/devices`
- placeholder device assembly guide at `/build`
- device popup with latest readings grouped by sensor type
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
- map status, metric legend, and metric selector are placed in left-side
  overlays
- device detail page with sensor charts from
  `GET /v1/devices/:public_id/measurements?period=<period>`

Potential future account flows remain out of scope for the current portal map
implementation.

## Working Stack Direction

The current working direction for the portal is:

- `Next.js` as the frontend application framework
- `React` as the UI layer
- `TypeScript` for application code

`Next.js` is a good fit here because the portal needs both:

- public marketing-style pages
- authenticated application pages for the personal account

The map uses `maplibre-gl` with OpenStreetMap raster tiles and GeoJSON-backed
device layers. The device detail page uses `recharts` for time-series charts.
Exact choices for future form libraries and auth helpers are still open and
should be selected later to match the actual backend auth model.

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
- `GET /v1/devices/:public_id/measurements?period=<period>`

The portal should not communicate directly with firmware devices for account workflows. Those interactions should go through the backend API layer.

Based on the current backend structure, versioned API routes under `/v1` are the expected integration direction, but the exact contract should follow the implemented backend endpoints.

## Current Status

- `backend/` already exists as a separate Fastify service
- `firmware/` already exists as the device-side implementation
- `portal/` now exists as a `Next.js` project with a placeholder home page,
  public map, placeholder device assembly guide, and device pages
- Ubuntu run and deployment instructions live in `docs/portal/ubuntu-deployment.md`

This document records the working scope and boundary for the portal as implementation begins.
