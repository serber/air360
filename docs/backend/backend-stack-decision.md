# Air360 Backend Stack Decision

## Status

Accepted working direction for the first backend and user UI implementation.

## Selected Stack

- `Next.js`
  - user-facing web UI
  - server-rendered pages
  - ordinary application API routes
- `Fastify`
  - device ingest API
  - upload-oriented HTTP endpoints
- `PostgreSQL`
  - users
  - devices
  - API keys and tokens
  - ownership and administrative data
  - idempotency and ingest control-plane metadata
- `TimescaleDB`
  - time-series sensor measurements
  - public device history
  - chart/query backend for historical data
- `shadcn/ui` + `Recharts`
  - user UI components
  - dashboard and history charts

## Why This Split

- `Next.js` is a better fit for the user portal, account pages, dashboards, and normal application flows than a firmware-style ingest service.
- `Fastify` is a better fit for device upload traffic than putting the ingest path inside the UI app.
- `PostgreSQL` remains the source of truth for user and device authorization state.
- `TimescaleDB` is the primary store for sensor measurements and public time-series queries.
- Sensor telemetry stays separate from user/account data even though authorization is checked against `PostgreSQL`.

## Intended Request Flow

1. Device sends an ingest request to the backend.
2. The ingest service validates the device and bearer token against data stored in `PostgreSQL`.
3. If authorization succeeds, the service writes normalized measurements to `TimescaleDB`.
4. Public and user-facing charts read historical telemetry from `TimescaleDB`.

## Notes

- One user may own multiple devices.
- Device measurements are intended to be publicly viewable.
- `PostgreSQL` and `TimescaleDB` may be deployed together for the first version, but they serve different roles.
- `Prometheus`-style storage is not the primary historical store for device telemetry in this architecture.

## Out of Scope For This Decision

- Exact monorepo structure
- Exact auth provider
- Deployment topology
- Background job/queue system
- Air360 portal visual design
