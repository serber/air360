# Air360 Backend Stack Decision

## Status

Accepted working direction for the first native Air360 API backend.

This document covers the backend service only.
The user-facing frontend is expected to live as a separate application and is out of scope here.

## Selected Backend Stack

- `Fastify`
  - device ingest API
  - upload-oriented HTTP endpoints
  - versioned backend routes such as `/v1/...`
- `PostgreSQL`
  - users
  - devices
  - API keys and tokens
  - ownership and administrative data
  - idempotency and ingest control-plane metadata
- `TimescaleDB`
  - time-series sensor measurements
  - public device history
  - historical query backend

## Why This Backend Shape

- `Fastify` is a good fit for a small, explicit HTTP API that is focused on device upload traffic.
- Separating the backend from the frontend keeps the ingest path independent from UI concerns.
- `PostgreSQL` remains the source of truth for user and device authorization state.
- `TimescaleDB` is the primary store for sensor measurements and historical queries.
- Sensor telemetry stays separate from user and account data even though authorization is checked against `PostgreSQL`.

## Intended Request Flow

1. Device sends an ingest request to the backend.
2. The ingest service validates the device and bearer token against data stored in `PostgreSQL`.
3. If authorization succeeds, the service writes normalized measurements to `TimescaleDB`.
4. Other services such as a frontend or public history API read historical telemetry from `TimescaleDB`.

## Current Backend Skeleton

Based on the current `/backend` implementation:

- the backend is scaffolded as a standalone Fastify application
- the current versioned route prefix is `/v1`
- `GET /` and `GET /health` exist for basic service checks
- `PUT /v1/devices/{chip_id}/batches/{client_batch_id}` exists as a mock ingest endpoint
- persistence and auth are not implemented yet

Implementation details should be verified against the `/backend` source tree rather than this design note.

## Notes

- one user may own multiple devices
- device measurements are intended to be publicly viewable
- `PostgreSQL` and `TimescaleDB` may be deployed together for the first version, but they serve different roles
- `Prometheus`-style storage is not the primary historical store for device telemetry in this architecture

## Out of Scope For This Decision

- frontend framework choice
- exact frontend deployment topology
- exact auth provider
- background job or queue system
- database schema details
- long-term monorepo structure
