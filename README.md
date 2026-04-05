# Air360

Air360 is a multi-part repository around an ESP32-S3 air-quality device, its native backend API, and a separate web portal.

The repository intentionally separates:

- `docs/` for architecture, planning, deployment notes, and implementation-oriented navigation
- `firmware/` for the actual ESP-IDF device firmware
- `backend/` for the native Air360 API backend
- `portal/` for the user-facing web application

When documentation and code disagree, treat the relevant implementation directory as the source of truth for current behavior.

## Overview

Based on the current tree, the repository now contains more than the original firmware replacement effort:

- an ESP-IDF firmware runtime for `esp32s3`
- a Fastify-based Air360 backend scaffold with a mock ingest route
- a Next.js portal scaffold
- design and deployment documentation that connects those pieces

The older planning documents in `docs/` still matter, but they should be read as design context unless the implementation confirms them.

## Repository Layout

```text
.
├── backend/
├── docs/
├── firmware/
├── portal/
├── AGENTS.md
├── LICENSE
└── README.md
```

- `docs/`
  Project context, architecture notes, compatibility analysis, firmware docs, backend docs, and portal deployment notes.
- `firmware/`
  ESP-IDF firmware project for the device. This is the source of truth for implemented device behavior.
- `backend/`
  Native Air360 API backend in TypeScript/Fastify.
- `portal/`
  Next.js portal application.
- `AGENTS.md`
  Repository-level guidance. In particular, it separates design context in `docs/` from implementation truth in `firmware/`.

## Documentation Map

Start here depending on what you need.

- Repository and architecture context:
  [docs/modern-replacement-firmware-architecture.md](docs/modern-replacement-firmware-architecture.md)
- Firmware implementation plan and phase history:
  [docs/firmware-iterative-implementation-plan.md](docs/firmware-iterative-implementation-plan.md)
- Hardware bring-up notes:
  [docs/phase-1-hardware-boot-notes.md](docs/phase-1-hardware-boot-notes.md)
  and [docs/phase-2-onboarding-hardware-notes.md](docs/phase-2-onboarding-hardware-notes.md)
- Legacy compatibility context:
  [docs/airrohr-firmware-server-contract.md](docs/airrohr-firmware-server-contract.md)
  and [docs/airrohr-firmware-ui-analysis.md](docs/airrohr-firmware-ui-analysis.md)
- Firmware documentation map:
  [docs/firmware/README.md](docs/firmware/README.md)
- Firmware end-user manual:
  [docs/firmware/user-guide.md](docs/firmware/user-guide.md)
- Firmware release-packaging skill:
  [.agents/skills/air360-firmware-release-bundle/](.agents/skills/air360-firmware-release-bundle/)
- Backend documentation map:
  [docs/backend/README.md](docs/backend/README.md)
- Portal documentation map:
  [docs/portal/README.md](docs/portal/README.md)

## Components

### Firmware

The buildable device application lives in [firmware/README.md](firmware/README.md).

Current firmware implementation includes:

- ESP-IDF 6.x application for `esp32s3`
- persisted device, sensor, and backend configuration in NVS
- station-mode join with SNTP time sync, background time-sync retry, and setup AP fallback
- local web UI at `/`, `/status`, `/config`, `/sensors`, and `/backends`
- setup AP onboarding at `/config` with scanned SSID list from `/wifi-scan`
- embedded frontend assets under `firmware/main/webui/`, served by the firmware at `/assets/*`
- category-based sensor configuration, background polling, and bounded measurement queueing
- backend upload support for `Sensor.Community` and `Air360 API`

### Backend

The native API backend lives in [backend/README.md](backend/README.md).

Current backend implementation is still early:

- Fastify scaffold in TypeScript
- `GET /` and `GET /health`
- `PUT /v1/devices/:chip_id/batches/:client_batch_id` as a mock ingest endpoint with basic payload validation

### Portal

The web portal lives in [portal/README.md](portal/README.md).

Current portal implementation is also early:

- Next.js 16 / React 19 / TypeScript scaffold
- single landing page in `src/app/page.tsx`
- deployment notes under `docs/portal/`

## Getting Started

If you need to work on device behavior:

1. Read [firmware/README.md](firmware/README.md)
2. Then use [docs/firmware/README.md](docs/firmware/README.md) as the firmware documentation map

If you need to publish a firmware beta or stable build:

1. Read [firmware/README.md](firmware/README.md) for the release packaging workflow
2. Use [.agents/skills/air360-firmware-release-bundle/](.agents/skills/air360-firmware-release-bundle/) to generate the versioned release bundle from `firmware/build/`

If you need to operate or provision a device rather than change firmware code:

1. Read [docs/firmware/user-guide.md](docs/firmware/user-guide.md)
2. Then use [firmware/README.md](firmware/README.md) only for build, flash, and implementation details

If you need repository context first:

1. Read this file
2. Read [docs/modern-replacement-firmware-architecture.md](docs/modern-replacement-firmware-architecture.md)
3. Read [docs/firmware-iterative-implementation-plan.md](docs/firmware-iterative-implementation-plan.md)

If you need backend work:

1. Read [backend/README.md](backend/README.md)
2. Read [docs/backend/README.md](docs/backend/README.md)

If you need portal work:

1. Read [portal/README.md](portal/README.md)
2. Read [docs/portal/README.md](docs/portal/README.md)

## Current Status

Current implementation status appears to be:

- `firmware/` is the most substantial implemented part of the repository
- `backend/` is a minimal but real API scaffold with a mock ingest path
- `portal/` is a minimal frontend scaffold
- many documents in `docs/` still describe intended direction, rollout phases, and compatibility constraints rather than completed work

## Development Notes

- Keep repository-level docs focused on project navigation and boundaries between subsystems.
- Keep firmware implementation details in `firmware/README.md` and `docs/firmware/`.
- Keep backend and portal operational details in their own directories and docs.
- Do not treat generated files under `firmware/build/` as maintained source.
- Preserve the distinction between planned behavior in `docs/` and implemented behavior in `firmware/`, `backend/`, and `portal/`.
