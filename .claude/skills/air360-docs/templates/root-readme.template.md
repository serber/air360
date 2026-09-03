# Air360

<!-- One paragraph: what the project is (open air-quality station), which parts live
     in this repository (firmware, backend, portal, docs), and who it is for. -->

## Overview

The repository contains:

- an ESP-IDF firmware for `esp32s3` (`firmware/`) — sensors, local web UI, multi-target uploads
- a Fastify backend (`backend/`) — device registration, authenticated ingest, public data
- a Next.js public portal (`portal/`)
- documentation (`docs/`) that connects those pieces and an end-user build guide

<!-- Keep this list factual; every bullet must correspond to a directory that exists. -->

## Repository Layout

```text
.
├── docs/
│   ├── firmware/    firmware implementation docs (architecture, subsystems, sensors, ADRs)
│   ├── backend/     backend design and deployment notes
│   ├── portal/      portal scope and boundaries
│   ├── guide/       end-user build guide
│   └── hardware/    shield and solar-module photos, Gerbers
├── firmware/        ESP-IDF project (source of truth for device behaviour)
├── backend/         Fastify application (source of truth for the native API)
├── portal/          Next.js application (source of truth for the public portal)
├── scripts/         repository checkers
├── .claude/skills/  agent skills
└── CLAUDE.md        project-wide agent contract
```

## Documentation Map

<!-- One line per entry point, as a relative link that exists. Group by audience:
     users (guide), firmware developers, backend/portal developers, releases. -->

- Build guide (assemble, flash, configure): `docs/guide/README.md`
- Firmware documentation map: `docs/firmware/README.md`
- Firmware architecture: `docs/firmware/ARCHITECTURE.md`
- Firmware startup sequence: `docs/firmware/startup-pipeline.md`
- Firmware configuration reference: `docs/firmware/configuration-reference.md`
- Backend documentation map: `docs/backend/README.md`
- Portal documentation map: `docs/portal/README.md`
- Release packaging skill: `.claude/skills/air360-firmware-release-bundle/SKILL.md`

## Components

### Firmware

<!-- Target, toolchain, what it does at runtime, where to read next, how to build
     (the canonical build command from firmware/CLAUDE.md). -->

### Backend

<!-- Stack, responsibilities, where the contract docs live, how to run. -->

### Portal

<!-- Stack, responsibilities, how to run. -->

## Getting Started

<!-- Three paths: I want to build a device (guide); I want to change firmware
     (firmware/CLAUDE.md + docs/firmware/README.md); I want to run backend/portal. -->

## Current Status

<!-- Only claims backed by code or by an implementation doc. Separate
     "implemented" from "documented, unverified" from "planned". -->

## Development Notes

<!-- Agent contracts (CLAUDE.md files), checkers in scripts/, skills, commit
     expectations. -->
