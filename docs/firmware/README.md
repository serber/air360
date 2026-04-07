# Air360 Firmware Docs

This directory contains implementation-oriented documentation for the ESP-IDF firmware project in [`../../firmware/`](../../firmware/).

These documents are written from the current `firmware/` source tree. They are meant to make the codebase easier to navigate, but `firmware/` remains the source of truth for implemented behavior.

## What Is In Scope

The current firmware is an ESP-IDF 6.x project for `esp32s3` that:

- boots a C++17 runtime around `air360::App`
- persists device, sensor, and backend configuration in NVS
- brings up either Wi-Fi station mode or setup AP mode
- synchronizes UTC time through SNTP when station uplink is available
- exposes a local web UI at `/`, `/status`, `/config`, `/sensors`, and `/backends`
- serves shared CSS, JavaScript, and page templates from embedded files under `firmware/main/webui/`
- runs a background sensor manager for supported drivers
- runs a background upload manager for supported remote backends
- renders a compact `Health` summary in `Overview` and exposes the same derived state in `/status`

## Document Map

- [`architecture.md`](architecture.md)
  Startup flow, runtime model, module boundaries, and steady-state behavior.
- [`configuration.md`](configuration.md)
  `Kconfig`, `sdkconfig.defaults`, runtime NVS config, partition layout, and board-level defaults.
- [`user-guide.md`](user-guide.md)
  End-user walkthrough for setup AP onboarding, station-mode usage, sensors, backends, and the local web UI.
- [`project-structure.md`](project-structure.md)
  Source tree walkthrough, component layout, build inputs, and developer navigation.
- [`sensors.md`](sensors.md)
  Sensor subsystem architecture, registry/runtime model, supported drivers, generic measurements, transports, and current board wiring assumptions.
- [`planned-device-support.md`](planned-device-support.md)
  Forward-looking inventory of planned sensors, peripherals, and connectivity modules. This file is planning-oriented, not a record of what is already implemented.
- [`platform-selection.md`](platform-selection.md)
  Engineering notes for choosing a hardware baseline for the current firmware, including why the present implementation fits ESP32-S3 best and how ESP32-C3, ESP32-C6, and ESP8266 compare as future alternatives.
- [`adr/README.md`](adr/README.md)
  Firmware architecture decision records for implemented and planned changes such as measurement/runtime separation, live sensor apply, and cellular uplink.
- [`../../firmware/README.md`](../../firmware/README.md)
  Operational firmware README with build, flash, monitor, startup, upload, and known-limitation details.
- [`../../.agents/skills/air360-firmware-release-bundle/`](../../.agents/skills/air360-firmware-release-bundle/)
  Repo-local skill for turning the current `firmware/build/` outputs into a GitHub-release-ready bundle with merged image, split images, zip archives, checksums, and release notes.

## How To Use These Docs

- Start with [`project-structure.md`](project-structure.md) if you are new to the firmware tree.
- Read [`../../firmware/README.md`](../../firmware/README.md) first if you need the practical build and runtime overview.
- Use [`user-guide.md`](user-guide.md) when the audience is a device user rather than a firmware developer.
- Read [`architecture.md`](architecture.md) to understand boot flow and service ownership.
- Use [`configuration.md`](configuration.md) when changing defaults, `sdkconfig`, or partitions.
- Use [`sensors.md`](sensors.md) before adding a new driver or changing sensor setup behavior.
- Use [`../../.agents/skills/air360-firmware-release-bundle/`](../../.agents/skills/air360-firmware-release-bundle/) when preparing a beta or stable firmware release from an existing local build.
- Use [`planned-device-support.md`](planned-device-support.md) when discussing future hardware support beyond what is already implemented.
- Use [`platform-selection.md`](platform-selection.md) when discussing whether Air360 should stay on ESP32-S3, move to ESP32-C3 or ESP32-C6, or attempt an ESP8266 port.
- Use [`adr/README.md`](adr/README.md) when the task is about planned firmware architecture changes rather than already implemented behavior.

## Scope Boundary

This directory mostly documents the current firmware implementation.

Planning and compatibility notes for the wider replacement effort still live in the parent [`../`](../) directory. Those files are useful design context, but they should not be treated as proof that a feature already exists in `firmware/`.

Exceptions in this directory are [`planned-device-support.md`](planned-device-support.md) and [`platform-selection.md`](platform-selection.md), which are intentionally more decision-oriented and forward-looking than the implementation walkthroughs. They should be treated as planning and compatibility context rather than proof that a feature or target already exists in `firmware/`.
