# Air360 backend guidance

## Scope

- This file is the backend-local working contract for AI agents operating inside `backend/`.
- `backend/` is the source of truth for implemented native Air360 API behavior.
- `../docs/backend/` explains the backend API, data model, ADRs, and deployment flow.
- Firmware upload contracts are shared with `../firmware/` and `../docs/firmware/upload-adapters.md`; verify both sides when changing ingest payloads.
- Portal read contracts are shared with `../portal/` and `../docs/portal/`; verify both sides when changing public read endpoints.

## Commands

Run commands from this directory.

```bash
npm install
npm run dev
npm run build
npm run typecheck
npm run migrate
npm start
```

`DATABASE_URL` is required for server startup and migrations. Copy `.env.example` to `.env` for local development.

## Read first

| If the task is about... | Read first |
|-------------------------|------------|
| Backend docs and API map | `../docs/backend/README.md` |
| Backend stack boundaries | `../docs/backend/backend-stack-decision.md` |
| Device registration or upload secrets | `../docs/backend/adr/implemented-firmware-generated-upload-secret-adr.md` |
| Portal-facing read APIs | `../docs/backend/adr/implemented-portal-api-extensions-adr.md` |
| Ubuntu deployment | `../docs/backend/backend-ubuntu-deployment-guide.md` |

## First code files to inspect

- Startup: `src/server.ts`, `src/app.ts`
- Config: `src/config/env.ts`
- Routes: `src/routes/index.ts`, `src/routes/v1/index.ts`, `src/routes/v1/devices.ts`, `src/routes/v1/ingest.ts`, `src/routes/v1/measurements.ts`
- Database: `src/db/client.ts`, `src/db/schema.ts`, `migrations/*`
- Devices: `src/modules/devices/device-repository.ts`
- Ingest: `src/modules/ingest/ingest-repository.ts`
- Measurements: `src/modules/measurements/measurement-repository.ts`
- Reverse geocoding: `src/modules/geo/geo-worker.ts`, `src/modules/geo/reverse-geocoder.ts`, `src/modules/geo/geo-queue-repository.ts`
- Shared contracts: `src/contracts/sensor-type.ts`, `src/contracts/measurement-kind.ts`

## Implemented API surface

- `GET /`
- `GET /v1/devices`
- `GET /v1/devices/offline`
- `GET /v1/devices/:public_id/measurements?period=<period>`
- `PUT /v1/devices/:device_id/register`
- `PUT /v1/devices/:device_id/batches/:batch_id`

There is no implemented `GET /v1/devices/:public_id/latest` route in the current backend.

## Co-change expectations

- If you add, remove, or reshape routes, update `../docs/backend/README.md` and any matching ADR under `../docs/backend/adr/`.
- If a route is consumed by the portal, update `../portal/src/lib/api.ts`, relevant portal components, and `../docs/portal/README.md`.
- If firmware payloads, auth, sensor types, or measurement kinds change, update the matching firmware upload docs and verify firmware code that generates the payload.
- If migrations or `src/db/schema.ts` change, update the backend data model docs and deployment guide if operators need to run or reason about the migration.
- If reverse-geocoding behavior changes, update the backend docs and any portal docs that display `geo_*` fields.
- If environment variables change, update `.env.example`, `../docs/backend/README.md`, and deployment docs.

## Notes

- `device_id` is the internal 48-bit hardware identifier from firmware.
- `public_id` is the UUID exposed through public read APIs.
- Ingest auth uses `Authorization: Bearer <upload_secret>` against the stored `sha256:<base64url(sha256(upload_secret))>` hash.
- `startGeoWorker()` runs inside the Fastify process and uses Nominatim reverse geocoding at a throttled interval.
