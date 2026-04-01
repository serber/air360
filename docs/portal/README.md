# Air360 Portal Docs

## Scope

This directory captures the working direction for the future Air360 user portal.

The portal is planned as a separate web application. It is not part of `firmware/`, and it is not the same application as `backend/`.

The `portal/` source tree now exists as a standalone `Next.js` application scaffold. This document remains the high-level planning note, while implementation details should be verified against the actual `portal/` codebase.

## Planned Portal Role

The portal is intended to cover two main areas:

- a public home page that explains the Air360 project and acts as a landing page
- a private user area for account access and device management

The first planned user-facing flows are:

- landing page with project overview and navigation links
- user registration
- user authorization and sign-in
- personal account area
- device list for the signed-in user
- device management actions inside the personal account

## Working Stack Direction

The current working direction for the portal is:

- `Next.js` as the frontend application framework
- `React` as the UI layer
- `TypeScript` for application code

`Next.js` is a good fit here because the portal needs both:

- public marketing-style pages
- authenticated application pages for the personal account

Exact choices for UI component libraries, form libraries, and auth helpers are still open. They should be selected later to match the actual backend auth model.

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

Initial integration areas will likely include:

- registration API
- sign-in API
- current user or session API
- user device list API
- device management APIs

The portal should not communicate directly with firmware devices for account workflows. Those interactions should go through the backend API layer.

Based on the current backend structure, versioned API routes under `/v1` are the expected integration direction, but the exact contract should follow the implemented backend endpoints.

## Current Status

- `backend/` already exists as a separate Fastify service
- `firmware/` already exists as the device-side implementation
- `portal/` now exists as a generated `Next.js` project with one initial landing page
- Ubuntu run and deployment instructions live in `docs/portal/ubuntu-deployment.md`

This document records the working scope and boundary for the portal as implementation begins.
