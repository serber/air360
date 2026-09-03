---
name: firmware-subsystem-walkthrough
description: "Explain a specific Air360 firmware subsystem with the right reading order, source files, and related docs. Use when mapping boot, power gate, networking, cellular, sensors, web UI, uploads, or storage for a contributor or agent."
---

# Firmware Subsystem Walkthrough

## When to use this skill

Use this skill when the user asks to:

- explain a firmware subsystem
- map which files own a behavior
- identify where to start reading for a bug or feature
- produce a subsystem-specific reading order for an AI agent

## Scope

- `firmware/CLAUDE.md` — task → doc routing table
- `docs/firmware/*.md` — subsystem docs with `Source of truth in code`
- `.claude/skills/esp-idf-cpp-developer/references/air360-map.md` — concern → code → doc
- `firmware/main/src/**`, `firmware/main/include/air360/**`

## Subsystem entry points

| Subsystem | Doc | First code file |
|-----------|-----|-----------------|
| Boot order, facades | `startup-pipeline.md` | `main/src/app.cpp` |
| Power gate / deep sleep | `power-gate.md` | `main/src/power_gate.cpp` |
| Wi-Fi, setup AP, SNTP, mDNS | `network-manager.md`, `time.md` | `main/src/network_manager.cpp` |
| Cellular / modem | `cellular-manager.md`, `sensors/sim7600e.md` | `main/src/cellular_manager.cpp` |
| Sensors, warmup, startup phases | `sensors/README.md`, `measurement-pipeline.md` | `main/src/sensors/sensor_manager.cpp`, `sensor_registry.cpp` |
| Persisted config | `nvs.md`, `configuration-reference.md` | `main/src/config_repository.cpp` and the other `*_config_repository.cpp` |
| Web UI and routes | `web-ui.md` | `main/src/web_server.cpp`, `main/src/web/*.cpp`, `main/webui/*` |
| Status pages and JSON | `web-ui.md`, `ARCHITECTURE.md` | `main/src/status_service.cpp` |
| Uploads and backends | `measurement-pipeline.md`, `upload-adapters.md`, `upload-transport.md` | `main/src/uploads/upload_manager.cpp` |
| OTA | `adr/implemented-ota-firmware-update-adr.md`, `web-ui.md` | `main/src/ota_service.cpp` |
| Watchdog and tasks | `watchdog.md`, `startup-pipeline.md` | each task's `taskMain()` |

## Workflow

1. Pick the row above (or the route in `firmware/CLAUDE.md`).
2. Open the doc; read its `Source of truth in code` list and the section for the behaviour in question.
3. Open the code files in that order: header, implementation, then every call site (`grep -rn`).
4. Summarize ownership boundaries, runtime flow (which task runs what), locking and
   notification points, and likely co-change files from `change-impact-map.md`.
5. Point to the next 2–3 docs the reader should open.

## Output shape

- what the subsystem is responsible for and what it explicitly does not own
- which files are authoritative
- which docs to read first
- what usually changes together
- known sharp edges (timer callbacks, TWDT, `-Werror`, config blob resets) if relevant
