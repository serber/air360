# Air360

## Overview

Air360 is a repository that combines project documentation and the ESP-IDF firmware implementation.

This repository is organized into two main parts:

- `docs/` — architecture notes, analysis, planning, and project context
- `firmware/` — the actual ESP-IDF firmware project

Use this repository if you need to understand the project direction, inspect implementation details, or work on the firmware.

---

## Repository layout

```text
.
├── docs/
├── firmware/
├── CLAUDE.md
├── LICENSE
└── ...
```

### `docs/`

The `docs/` directory contains project documentation such as:

- architecture notes
- implementation plans
- hardware bring-up notes
- backend/server contract notes
- UI and integration analysis

These files are useful for understanding project intent, constraints, and planned direction.

### `firmware/`

The `firmware/` directory contains the buildable ESP-IDF application.

This is the main implementation area for:

- application source code
- firmware configuration
- partition layout
- build and flash workflow

### `CLAUDE.md`

Project-wide guidance for Claude Code and contributors.

---

## Documentation map

The following files are the main entry points in `docs/`:

### `docs/modern-replacement-firmware-architecture.md`

Describes the intended firmware architecture, major subsystems, and design direction.

### `docs/firmware-iterative-implementation-plan.md`

Describes the staged implementation approach and likely delivery phases.

### `docs/phase-1-hardware-boot-notes.md`

Contains hardware bring-up notes and early boot observations.

### `docs/airrohr-firmware-server-contract.md`

Describes the expected contract between firmware and server/backend.

### `docs/airrohr-firmware-ui-analysis.md`

Contains UI-facing or product-facing analysis relevant to the firmware behavior.

> Note: files in `docs/` may describe planned or intended behavior. Confirm implementation details against the firmware sources in `firmware/`.

---

## Firmware location

The buildable ESP-IDF project lives in:

```text
firmware/
```

Typical firmware-related files include:

- `firmware/CMakeLists.txt`
- `firmware/main/`
- `firmware/sdkconfig`
- `firmware/sdkconfig.defaults`
- `firmware/partitions.csv`
- `firmware/README.md`

For implementation details, treat `firmware/` as the source of truth.

---

## Getting started

Choose your starting point depending on your goal.

### If you want to understand the project structure

Read:

1. this README
2. `docs/modern-replacement-firmware-architecture.md`
3. `firmware/README.md`

### If you want to understand hardware and early bring-up

Read:

1. `docs/phase-1-hardware-boot-notes.md`
2. relevant firmware source files under `firmware/main/`

### If you want to build or modify the firmware

Go to:

1. `firmware/README.md`
2. `firmware/main/`
3. `firmware/sdkconfig.defaults`
4. `firmware/partitions.csv`

---

## Current status

Based on the repository structure:

- `docs/` captures architecture, planning, and analysis context
- `firmware/` contains the active implementation
- implementation status should always be verified against the source code in `firmware/`

When documentation and code differ, prefer the firmware source tree for current behavior.

---

## Development notes

- Keep repository-level documentation and firmware-level documentation separate.
- Use `docs/` for design rationale and broader project context.
- Use `firmware/` documentation for implementation and operational instructions.
- Avoid treating generated files under `firmware/build/` as maintained source files.

---

## Contributing

When updating documentation:

- preserve project-specific terminology
- distinguish implemented behavior from planned behavior
- keep root-level docs focused on repository navigation and context
- keep firmware-specific instructions inside `firmware/README.md`
