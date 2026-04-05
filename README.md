# Air360

Air360 is a multi-part repository around an ESP32-S3 air-quality device, a modern ESP-IDF firmware for that device, a native Air360 backend API, and a separate web portal.

At the device level, the project is centered on a local-first firmware runtime with setup AP onboarding, station-mode web UI, configurable sensor support, and backend uploads. The repository also contains the beginning of the Air360 backend and portal that are intended to grow around that device runtime.

Air360 is not a replacement for the Sensor.Community ecosystem in the sense of breaking compatibility with it. The current firmware keeps explicit compatibility with the Sensor.Community upload flow, so an Air360 device can still be registered on `devices.sensor.community` and upload measurements to the Sensor.Community endpoint while also supporting the native `Air360 API` backend.

The repository intentionally separates:

- `docs/` for architecture, planning, deployment notes, and implementation-oriented navigation
- `firmware/` for the actual ESP-IDF device firmware
- `backend/` for the native Air360 API backend
- `portal/` for the user-facing web application

When documentation and code disagree, treat the relevant implementation directory as the source of truth for current behavior.

## Overview

Based on the current tree, the repository now contains more than the original firmware replacement effort:

- an ESP-IDF firmware runtime for `esp32s3`
- compatibility with the Sensor.Community upload endpoint and registration flow
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

- Firmware architecture:
  [docs/firmware/architecture.md](docs/firmware/architecture.md)
- Firmware configuration and source layout:
  [docs/firmware/configuration.md](docs/firmware/configuration.md)
  and [docs/firmware/project-structure.md](docs/firmware/project-structure.md)
- Firmware sensor subsystem:
  [docs/firmware/sensors.md](docs/firmware/sensors.md)
- Planned future device support:
  [docs/firmware/planned-device-support.md](docs/firmware/planned-device-support.md)
- Sensor.Community opportunity roadmap:
  [docs/ecosystem/sensor-community-opportunity-roadmap.md](docs/ecosystem/sensor-community-opportunity-roadmap.md)
- Mobile uplink ADR:
  [docs/sim7600e-mobile-uplink-adr.md](docs/sim7600e-mobile-uplink-adr.md)
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
- `Sensor.Community` compatibility through the same device identity model exposed in the firmware UI as `Short ID`

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
2. For release binaries, flash the merged `full.bin` through `https://espflash.app/`
3. Then use [firmware/README.md](firmware/README.md) only for build, flash, and implementation details

If you need to understand the Sensor.Community overlap specifically:

1. Read this file for the project-level compatibility statement
2. Read [docs/firmware/user-guide.md](docs/firmware/user-guide.md) for the actual registration and backend setup flow

If you need repository context first:

1. Read this file
2. Read [docs/firmware/README.md](docs/firmware/README.md)
3. Read [docs/firmware/architecture.md](docs/firmware/architecture.md)

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
