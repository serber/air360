# Air360 Firmware

## Overview

This directory contains the ESP-IDF firmware for the Air360 project.

The firmware source of truth is located in this directory, including:

- application code
- project configuration
- partition layout
- build and flash workflow

Use this document as the entry point for developers working on the firmware implementation.

---

## Project structure

```text
firmware/
├── CMakeLists.txt
├── main/
│   ├── CMakeLists.txt
│   ├── Kconfig.projbuild
│   ├── include/
│   └── src/
├── partitions.csv
├── sdkconfig
├── sdkconfig.defaults
└── README.md
```

### `main/`

Contains the application-specific source code.

### `main/include/`

Public headers and shared interfaces used by the firmware modules.

### `main/src/`

Implementation files for the firmware logic.

### `main/Kconfig.projbuild`

Project-specific configuration options exposed through menuconfig.

### `sdkconfig` and `sdkconfig.defaults`

Project configuration files for ESP-IDF.

### `partitions.csv`

Partition table used by the firmware image layout.

---

## Requirements

Document the required environment here.

Example items to specify:

- supported ESP-IDF version
- supported target chip
- host operating system assumptions
- required tools or Python environment
- serial flashing prerequisites

> Replace this section with project-specific requirements confirmed from the repository.

---

## Configuration

This project uses standard ESP-IDF configuration mechanisms.

Relevant configuration surfaces include:

- `sdkconfig.defaults` for project defaults
- `sdkconfig` for the active local build configuration
- `main/Kconfig.projbuild` for project-defined configuration options

Document here:

- which options are important for local development
- which options affect hardware behavior
- which options affect connectivity, logging, storage, or feature flags

---

## Build

Run build commands from the `firmware/` directory.

```bash
idf.py build
```

If a clean rebuild is needed:

```bash
idf.py fullclean
idf.py build
```

Add any project-specific build notes here.

---

## Flash

Flash the firmware with:

```bash
idf.py flash
```

If the serial port must be specified explicitly:

```bash
idf.py -p <PORT> flash
```

Add any board-specific or boot-mode notes here.

---

## Monitor

Open the serial monitor with:

```bash
idf.py monitor
```

Combined flash and monitor flow:

```bash
idf.py -p <PORT> flash monitor
```

Document expected log tags, boot messages, or troubleshooting notes here.

---

## Architecture overview

Summarize the actual firmware design here.

Recommended topics:

- entry point and startup flow
- major modules and responsibilities
- initialization order
- runtime loop or task model
- peripheral ownership
- storage and configuration boundaries
- external interfaces

Keep this section aligned with the actual source code under `main/`.

---

## Source layout

Document the main source modules here.

Suggested format:

### Module: `<name>`

**Purpose**
Describe what this module does.

**Key files**
List relevant files in `main/include/` and `main/src/`.

**Dependencies**
List important dependencies such as drivers, config, storage, networking, or shared utilities.

**Runtime behavior**
Describe whether the module is initialized once, runs continuously, owns a task, uses callbacks, etc.

Repeat for each major module.

---

## Configuration details

Document only meaningful project-specific settings.

Suggested topics:

- logging configuration
- Wi-Fi or network-related settings
- backend/server endpoint settings
- hardware feature toggles
- display or sensor settings
- debug-only configuration

Do not dump raw config without explanation.

---

## Storage and partitions

Document the partition layout defined in `partitions.csv`.

Suggested topics:

- purpose of each partition
- whether NVS is used
- whether OTA partitions exist
- whether any data partition is used by the application

Only describe behavior that is confirmed by code or partition configuration.

---

## Logging and debugging

Describe how to debug the firmware.

Suggested topics:

- relevant log tags
- expected boot output
- common startup failures
- serial monitor usage
- menuconfig options useful during debugging

---

## Known limitations

Document known limitations only if they are supported by code, comments, or project docs.

Examples:

- hardware support still incomplete
- some modules are stubbed or planned
- backend integration is partial
- specific features are not yet implemented

---

## Development notes

- Treat source files in `main/` as the implementation source of truth.
- Treat `docs/` as design context unless the code confirms the behavior.
- Keep this README implementation-focused.
- Avoid documenting generated files under `build/` as if they were maintained source files.
