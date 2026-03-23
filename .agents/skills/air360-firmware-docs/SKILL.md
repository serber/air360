---
name: air360-firmware-docs
description: Use this skill when the task is to create, update, audit, or improve documentation for the ESP-IDF firmware project located in ./firmware. Best for firmware README generation, build/flash/monitor instructions, architecture docs based on firmware/main, sdkconfig explanation, partition table notes, Kconfig documentation, source layout explanation, and implementation-focused developer documentation for the C++17 firmware.
---

# Air360 Firmware Documentation Skill

## When to use this skill

Use this skill when the user asks to:

- create or update `firmware/README.md`
- explain how to build, flash, or monitor the firmware
- document the ESP-IDF project structure
- explain `sdkconfig` or `sdkconfig.defaults`
- document `partitions.csv`
- summarize modules under `firmware/main/`
- describe startup flow or runtime architecture
- document C++17 source structure
- explain firmware configuration or developer workflow
- align firmware docs with the current implementation

Do not use this skill for repository-level documentation unless the main subject is clearly the firmware project.

---

## Firmware scope

Treat `firmware/` as the firmware project root.

Relevant files include:

- `firmware/CMakeLists.txt`
- `firmware/main/CMakeLists.txt`
- `firmware/main/Kconfig.projbuild`
- `firmware/main/include/**`
- `firmware/main/src/**`
- `firmware/README.md`
- `firmware/sdkconfig`
- `firmware/sdkconfig.defaults`
- `firmware/partitions.csv`

Use `docs/` only as supporting context for intent and design terminology. The firmware source tree is the source of truth for implementation.

---

## Main goals

When this skill is used, produce firmware documentation that helps a developer:

- understand what the firmware does
- build and flash it correctly
- navigate the source tree
- understand important config and partitioning choices
- understand startup flow and major modules
- distinguish current implementation from architectural intent

---

## Required inspection workflow

### Step 1. Read project-wide guidance

Check:

- root `AGENTS.md`

Respect repository-wide rules before writing firmware docs.

### Step 2. Inspect firmware project metadata

Check:

- `firmware/CMakeLists.txt`
- `firmware/main/CMakeLists.txt`
- `firmware/main/Kconfig.projbuild`
- `firmware/README.md`
- `firmware/sdkconfig`
- `firmware/sdkconfig.defaults`
- `firmware/partitions.csv`

From these files, infer:

- project naming
- target and build assumptions
- config surfaces
- partitioning scheme
- firmware entry points

### Step 3. Inspect implementation

Check source files in:

- `firmware/main/include/`
- `firmware/main/src/`

Look for:

- `app_main`
- initialization sequence
- service/module boundaries
- classes, namespaces, wrappers, and helpers
- FreeRTOS tasks, timers, queues, event groups, callbacks
- GPIO/peripheral usage
- network communication
- storage or persistence usage
- logging tags and error handling patterns

### Step 4. Cross-check design context

If needed, consult `docs/` for architectural vocabulary or planned direction, but do not document planned behavior as implemented unless the firmware code confirms it.

---

## Templates

Preferred templates for firmware documentation:

- `templates/firmware-readme.template.md`
- `templates/firmware-architecture.template.md`

When generating firmware documentation, start from the closest matching template and fill it with repository-specific details from the `firmware/` project.

Template rules:

- treat templates as structure, not final content
- prefer implementation evidence from `firmware/` over design intent from `docs/`
- remove sections that do not match the current firmware state
- replace all placeholders with confirmed project details
- do not leave generic ESP-IDF boilerplate unless it is useful for this project

---

## Output types

### 1. Firmware README

Use this structure by default:

# Air360 Firmware

## Overview
What the firmware is for.

## Project structure
Explain:
- `main/`
- `main/include/`
- `main/src/`
- config files
- partition table
- build output location only if useful

## Requirements
ESP-IDF and host assumptions if detectable.

## Configuration
Explain relevant `sdkconfig` and `Kconfig` surfaces.

## Build
Commands and workflow.

## Flash
Commands and notes.

## Monitor
How to inspect logs.

## Architecture overview
Startup flow, modules, and runtime responsibilities.

## Storage and partitions
Explain `partitions.csv` and related behavior.

## Debugging notes
Practical troubleshooting information.

## Known limitations
Only include limitations supported by code or docs.

### 2. Firmware architecture document

Use this structure by default:

# Air360 Firmware Architecture

## Purpose
## Firmware project layout
## Startup sequence
## Runtime model
## Components and responsibilities
## Configuration model
## Storage and partitions
## External interfaces
## Logging and error handling
## Open questions and future work

### 3. Firmware doc audit

When updating existing docs, compare:

- current firmware docs
- current firmware source structure
- actual configuration files

Then produce a focused update rather than rewriting everything by default.

---

## Configuration documentation rules

### `sdkconfig` and `sdkconfig.defaults`

When documenting config:

- summarize only project-relevant options
- explain why each option matters when inferable
- avoid dumping raw config values without context

### `Kconfig.projbuild`

When documenting Kconfig options:

- describe user-facing purpose
- explain impact on behavior
- note defaults only if relevant

---

## Partition table documentation rules

For `partitions.csv`:

- explain each partition only if its purpose is clear
- mention NVS, OTA, app, or data partitions if present
- do not assume runtime OTA logic exists unless code confirms it
- do not over-document partition internals beyond what is visible

---

## Build and generated files rules

### Canonical sources

Prefer hand-maintained files over generated files.

### `firmware/build/`

Ignore `firmware/build/` as a primary documentation source.

It may be used only for narrow factual hints such as:

- build artifacts existing
- target naming clues
- generated binary names

Do not present `build/` as maintained source structure.

---

## Writing rules

### Accuracy

- Prefer firmware code over design notes.
- Do not invent board, pin, sensor, or bus details.
- Do not invent tasks, classes, or modules.
- Mark uncertain statements as inferred.

### Style

- Write in clear Markdown.
- Prefer implementation-focused developer documentation.
- Keep instructions scoped to the `firmware/` directory.

### C++17 expectations

When applicable, document:

- namespaces
- classes and roles
- interface/implementation separation
- RAII or wrapper patterns if visible
- interaction between ESP-IDF C APIs and C++ code

### ESP-IDF terminology

Use correct terms such as:

- component
- sdkconfig
- Kconfig
- partition table
- boot
- flash
- monitor
- NVS
- FreeRTOS task
- app_main

---

## Good behavior for common requests

### If asked to "write README"

Assume `firmware/README.md` if the discussion is about ESP-IDF, build, flash, source modules, or config.

### If asked to "document firmware"

Focus on implementation first:
- source structure
- runtime flow
- configuration
- build and flash workflow

### If asked to "update docs"

Preserve useful project-specific text in `firmware/README.md`, remove stale or generic wording, and align the document with the current codebase.

---

## Definition of done

A firmware documentation task is complete only if:

- the documentation is correctly scoped to `firmware/`
- build/flash/monitor workflow is documented if relevant
- config and partition files are explained if relevant
- source structure and architecture reflect the actual implementation
- planned behavior is not overstated as current behavior
- the output is commit-ready Markdown

---

## Anti-patterns to avoid

Do not:

- write a generic ESP-IDF tutorial unrelated to this project
- use `docs/` as proof of implementation
- over-document generated files in `build/`
- invent hardware mappings or FreeRTOS constructs
- mix repo-level onboarding text into firmware docs unless clearly useful

---

## Example trigger phrases

Activate this skill for requests like:

- "Update firmware/README.md"
- "Document the ESP-IDF firmware project"
- "Explain how to build and flash the firmware"
- "Describe modules under firmware/main"
- "Summarize startup flow from the firmware source"
- "Document sdkconfig and partitions for this firmware"