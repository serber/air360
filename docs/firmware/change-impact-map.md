# Firmware Change Impact Map

## Status

Implemented. Keep this map aligned with the current `firmware/` tree and implementation docs.

## Scope

This document maps common firmware code changes to the other code, configuration, and documentation surfaces that usually need review in the same change.

## Source of truth in code

- `firmware/main/src/`
- `firmware/main/include/air360/`
- `firmware/main/Kconfig.projbuild`
- `firmware/sdkconfig.defaults`
- `firmware/partitions.csv`

## Read next

- [PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md)
- [configuration-reference.md](configuration-reference.md)
- [sensors/adding-new-sensor.md](sensors/adding-new-sensor.md)

## Co-change matrix

| If you change... | Review code in... | Review docs in... |
|------------------|-------------------|-------------------|
| `app_main.cpp`, `app.cpp` | `build_info.cpp`, config repositories, startup-owned services | `startup-pipeline.md`, `ARCHITECTURE.md`, sometimes `PROJECT_STRUCTURE.md` |
| `network_manager.cpp` | `connectivity_checker.cpp`, `status_service.cpp`, `web_server.cpp` | `network-manager.md`, `time.md`, `startup-pipeline.md`, `web-ui.md` |
| `cellular_manager.cpp`, `modem_gpio.cpp`, `cellular_config_repository.cpp` | `network_manager.cpp`, `status_service.cpp` | `cellular-manager.md`, `configuration-reference.md`, `sensors/sim7600e.md`, `startup-pipeline.md` |
| `config_repository.cpp` | `web_server.cpp`, `status_service.cpp` | `nvs.md`, `configuration-reference.md`, `startup-pipeline.md` |
| `sensors/sensor_config_repository.cpp` | `sensor_registry.cpp`, `web_server.cpp` | `nvs.md`, `configuration-reference.md`, `sensors/README.md`, `sensors/adding-new-sensor.md` |
| `uploads/backend_config_repository.cpp` | `backend_registry.cpp`, `upload_manager.cpp`, `web_server.cpp` | `nvs.md`, `configuration-reference.md`, `measurement-pipeline.md`, `upload-adapters.md` |
| `web_server.cpp`, `web_ui.cpp`, `main/webui/*` | `status_service.cpp`, config repositories, sensor registry | `web-ui.md`, `configuration-reference.md`, sometimes `network-manager.md` |
| `status_service.cpp` | `web_server.cpp`, network and upload state providers | `ARCHITECTURE.md`, `web-ui.md` |
| `sensors/sensor_manager.cpp` | `sensor_registry.cpp`, drivers, transport binding | `measurement-pipeline.md`, `sensors/README.md`, `transport-binding.md` |
| `sensors/sensor_registry.cpp` | per-driver headers, `web_server.cpp`, config repositories | `configuration-reference.md`, `sensors/README.md`, `sensors/supported-sensors.md`, `sensors/adding-new-sensor.md` |
| `sensors/transport_binding.cpp` | sensor drivers, `sensor_manager.cpp` | `transport-binding.md`, `ARCHITECTURE.md`, `sensors/README.md` |
| `sensors/drivers/*.cpp` or `*.hpp` | matching driver header/impl, registry, manager | matching `docs/firmware/sensors/*.md`, `sensors/README.md`, `sensors/supported-sensors.md`, sometimes `measurement-pipeline.md` |
| `uploads/upload_manager.cpp` | `measurement_store.cpp`, `backend_registry.cpp`, adapters | `measurement-pipeline.md`, `upload-adapters.md`, `upload-transport.md` |
| `uploads/measurement_store.cpp` | `upload_manager.cpp`, adapters | `measurement-pipeline.md`, related ADRs |
| `uploads/upload_transport.cpp` | upload adapters | `upload-transport.md`, `upload-adapters.md` |
| `uploads/adapters/*.cpp` | `backend_registry.cpp`, `upload_transport.cpp` | `upload-adapters.md`, `measurement-pipeline.md`, `configuration-reference.md` |
| `main/Kconfig.projbuild`, `sdkconfig.defaults` | consumers of affected `CONFIG_AIR360_*` values | `configuration-reference.md`, `ARCHITECTURE.md`, `transport-binding.md`, sensor docs, `firmware/README.md` |
| `partitions.csv` | any code relying on SPIFFS, OTA, or NVS assumptions | `firmware/README.md`, `ARCHITECTURE.md`, relevant ADRs |

## Task-specific reminders

- Route additions or removals in `web_server.cpp` should be reflected in `web-ui.md`.
- NVS blob layout changes should update `nvs.md` and the field-level reference in `configuration-reference.md`.
- Sensor additions are not complete until the registry, docs index, supported-sensors matrix, and per-driver page all agree.
- Upload behavior changes often touch both runtime flow docs and backend adapter docs.
- If a refactor changes ownership boundaries, review `ARCHITECTURE.md` even when behavior is unchanged.
