# Air360 API Backend

Minimal Fastify scaffold for the native Air360 API backend.

## Scope

This directory is intentionally limited to the backend API:

- device ingest endpoints
- internal backend modules
- future auth, persistence, and background processing integration

It does not contain the user-facing frontend application.

## Structure

- `src/server.ts` starts the HTTP server
- `src/app.ts` builds the Fastify instance
- `src/config/` contains environment parsing
- `src/plugins/` contains shared Fastify setup
- `src/routes/` contains route registration and versioned API namespaces
- `src/modules/` is reserved for future domain modules

## Commands

```bash
npm install
npm run dev
```

Production build:

```bash
npm run build
npm start
```

## Environment

Copy `.env.example` to `.env` and adjust values as needed.

## Current state

This is only a skeleton. The backend currently exposes:

- `GET /` for a simple service response
- `GET /health` for a health check
- `PUT /api/v1/devices/:chip_id/batches/:client_batch_id` as a not-yet-implemented ingest route

The actual business logic, auth checks, and persistence are not implemented yet.
