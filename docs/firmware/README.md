# Air360 Firmware Docs

This directory contains implementation-oriented documentation for the ESP-IDF firmware project in [`../../firmware/`](../../firmware/).

These documents are written from the current `firmware/` source tree. They are meant to make the codebase easier to navigate, but `firmware/` remains the source of truth for implemented behavior.

## What Is In Scope

The current firmware is an ESP-IDF 6.x project for `esp32s3` that boots a local configuration runtime, persists device and sensor configuration in NVS, brings up either Wi-Fi station mode or setup AP mode, exposes a local web UI at `/`, `/status`, `/config`, and `/sensors`, and runs a background sensor manager for supported sensor drivers.

## Document Map

- [`architecture.md`](architecture.md)
  Startup flow, runtime model, module boundaries, and steady-state behavior.
- [`configuration.md`](configuration.md)
  `Kconfig`, `sdkconfig.defaults`, runtime NVS config, partition layout, and board-level defaults.
- [`project-structure.md`](project-structure.md)
  Source tree walkthrough, component layout, build inputs, and developer navigation.
- [`sensors.md`](sensors.md)
  Sensor subsystem architecture, registry/runtime model, supported drivers, generic measurements, transports, and current board wiring assumptions.
- [`planned-device-support.md`](planned-device-support.md)
  Forward-looking inventory of planned sensors, peripherals, and connectivity modules. This file is planning-oriented, not a record of what is already implemented.

## How To Use These Docs

- Start with [`project-structure.md`](project-structure.md) if you are new to the firmware tree.
- Read [`architecture.md`](architecture.md) to understand boot flow and service ownership.
- Use [`configuration.md`](configuration.md) when changing defaults, `sdkconfig`, or partitions.
- Use [`sensors.md`](sensors.md) before adding a new driver or changing sensor setup behavior.
- Use [`planned-device-support.md`](planned-device-support.md) when discussing future hardware support beyond what is already implemented.

## Scope Boundary

This directory mostly documents the current firmware implementation.

Planning and compatibility notes for the wider replacement effort still live in the parent [`../`](../) directory. Those files are useful design context, but they should not be treated as proof that a feature already exists in `firmware/`.

One exception in this directory is [`planned-device-support.md`](planned-device-support.md), which is intentionally forward-looking and should also be treated as planning context rather than implemented behavior.
