# Air360

Air360 is a repository for a modern replacement firmware effort around the `sensor.community` device model.

The project combines two things:

- repository-level design and compatibility context in `docs/`
- the actual ESP-IDF firmware implementation in `firmware/`

The key distinction is important: `docs/` describes architecture, planning, and compatibility goals, while `firmware/` shows what is implemented today.

## Project Purpose

Based on the current repository contents, the goal is to build a new ESP32-first firmware in C++17 on ESP-IDF 6.x that can eventually replace the legacy `airrohr-firmware` while preserving the compatibility-critical parts of its behavior.

The replacement effort is not framed as a line-by-line port. The documents in `docs/` describe a cleaner architecture with explicit module boundaries, staged delivery, and adapter-based handling of legacy behavior.

The current implementation in `firmware/` is an early Phase 1 foundation. It already boots as an ESP-IDF project, persists a small device config in NVS, can start a lab-only SoftAP, and exposes a minimal local web interface.

## Repository Layout

```text
.
├── docs/
├── firmware/
├── AGENTS.md
├── LICENSE
└── README.md
```

- `docs/`
  Architecture notes, compatibility analysis, planning, and hardware bring-up context for the replacement firmware.
- `firmware/`
  The buildable ESP-IDF application. This is the source of truth for current behavior.
- `AGENTS.md`
  Project-wide guidance for documentation and automation. It explicitly separates design context from implementation truth.

## Role of `docs/`

The `docs/` folder captures the reasoning around the project:

- `docs/modern-replacement-firmware-architecture.md`
  Describes the intended long-term architecture, platform choices, and design principles.
- `docs/firmware-iterative-implementation-plan.md`
  Breaks the work into delivery phases and clarifies what should be built first.
- `docs/phase-1-hardware-boot-notes.md`
  Documents the current bring-up assumptions and what the first Phase 1 runtime is expected to do on hardware.
- `docs/airrohr-firmware-server-contract.md`
  Analyzes the legacy firmware's server communication so compatibility can be preserved intentionally.
- `docs/airrohr-firmware-ui-analysis.md`
  Analyzes the legacy onboarding and local UI behavior that informs migration and compatibility decisions.

Use `docs/` to understand intent, constraints, and roadmap. Do not treat it as proof that the new firmware already implements every planned behavior.

## Role of `firmware/`

The `firmware/` directory contains the actual ESP-IDF firmware project.

This is where to look for:

- the project root `CMakeLists.txt`
- the app component under `firmware/main/`
- project configuration in `sdkconfig`, `sdkconfig.defaults`, and `main/Kconfig.projbuild`
- flash layout in `partitions.csv`
- the current VS Code entry point in `firmware/firmware.code-workspace`

Based on the current source tree, the firmware currently includes:

- an `app_main` entry point and C++17 application structure
- NVS-backed config creation and loading
- a boot counter
- a lab-only SoftAP bring-up hook
- a minimal local HTTP server with `/` and `/status`

When documentation and code disagree, prefer `firmware/`.

## How a New Contributor Should Navigate the Repository

The easiest way to approach the repository is to choose a track.

If you need architecture and project context:

1. Read this `README.md`
2. Read `docs/modern-replacement-firmware-architecture.md`
3. Read `docs/firmware-iterative-implementation-plan.md`

If you need to understand what is already implemented:

1. Go to `firmware/`
2. Inspect `firmware/main/src/app.cpp`
3. Inspect `firmware/main/src/config_repository.cpp`
4. Inspect `firmware/main/src/network_manager.cpp`
5. Inspect `firmware/main/src/web_server.cpp`

If you need hardware bring-up context:

1. Read `docs/phase-1-hardware-boot-notes.md`
2. Cross-check the notes against the code in `firmware/`

If you need compatibility context from the legacy firmware:

1. Read `docs/airrohr-firmware-server-contract.md`
2. Read `docs/airrohr-firmware-ui-analysis.md`
3. Treat those as constraints for future implementation work, not as current implementation proof

For day-to-day firmware work in VS Code, open `firmware/` directly or open `firmware/firmware.code-workspace`.

## Current Status

Based on the current structure, the repository contains a real firmware foundation and a larger body of design and planning work around it.

Implemented now in `firmware/`:

- ESP-IDF CMake project for `esp32s3`
- C++17 application structure with explicit modules
- NVS-backed default device config and boot counter
- minimal status-oriented local web server
- lab AP bring-up path for early validation

Still primarily represented as design or planned work in `docs/`:

- fuller onboarding flow centered on `/config`
- broader sensor support
- backend upload compatibility slices
- later transport and expansion work

## Development Notes

- Keep repository-level documentation focused on project navigation and context.
- Keep firmware implementation details and operational instructions scoped to `firmware/`.
- Avoid treating generated files under `firmware/build/` as maintained source.
- Preserve the distinction between architectural intent in `docs/` and current implementation in `firmware/`.
