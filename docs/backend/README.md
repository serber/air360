# Air360 Backend Docs

## Scope

This directory contains design notes and operational documentation for the native Air360 API backend.

These documents describe backend architecture, ingest contract, and Ubuntu deployment flow.
They do not describe the ESP-IDF firmware implementation in `firmware/`, and they do not define the separate frontend application.

## Documentation Map

- `backend-stack-decision.md`
  - backend-only stack direction
  - Fastify, PostgreSQL, and TimescaleDB roles
  - current backend scaffold boundaries

- `air360-api-measurement-ingest-draft.md`
  - draft ingest API contract
  - current route shape at `PUT /v1/devices/{chip_id}/batches/{client_batch_id}`
  - request rules and minimal success response

- `backend-ubuntu-deployment-guide.md`
  - how to deploy the backend on a standalone Ubuntu host
  - Node.js, nginx, systemd, TLS, and update flow

- `air360-backend.service`
  - ready-to-copy `systemd` unit example

- `air360-backend.nginx.conf`
  - ready-to-copy `nginx` site config example

- `air360-backend-release-deploy.sh`
  - step-by-step release update script for the Ubuntu deployment flow

## Source Of Truth

- `/backend` is the source of truth for the currently implemented backend behavior
- `docs/backend/` captures intended contract, deployment guidance, and architecture notes

If a document here conflicts with the current backend source tree, implementation should be verified against `/backend`.
