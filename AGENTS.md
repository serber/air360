# Air360 project guidance

## Repository structure

- `docs/` contains architecture, analysis, planning, and project context documents.
- `docs/firmware/` contains implementation-oriented documentation for the firmware project, derived from the current `firmware/` tree.
- `firmware/` contains the actual ESP-IDF firmware project.

## Documentation rules

- Treat `firmware/` as the source of truth for implemented behavior.
- Treat `docs/firmware/` as explanatory documentation for the current firmware implementation, not as an independent source of truth.
- Treat `docs/` as design and planning context unless implementation confirms it.
- Keep repository-level docs and firmware-level docs clearly separated.
- Keep `docs/firmware/` focused on the firmware project and keep broader project planning in the parent `docs/` directory.
- Avoid documenting generated files in `firmware/build/` except when narrow factual hints are needed.
- Preserve project-specific terminology and avoid generic boilerplate.

## Firmware documentation navigation

Start at [`docs/firmware/README.md`](docs/firmware/README.md) — it is the index for all firmware implementation docs.

Key entry points:

| Goal | Document |
|------|----------|
| Understand the system at a glance | [docs/firmware/ARCHITECTURE.md](docs/firmware/ARCHITECTURE.md) |
| Trace the boot sequence | [docs/firmware/startup-pipeline.md](docs/firmware/startup-pipeline.md) |
| Understand config storage | [docs/firmware/nvs.md](docs/firmware/nvs.md) + [docs/firmware/configuration-reference.md](docs/firmware/configuration-reference.md) |
| Understand Wi-Fi / SNTP | [docs/firmware/network-manager.md](docs/firmware/network-manager.md) + [docs/firmware/time.md](docs/firmware/time.md) |
| Understand sensor → upload flow | [docs/firmware/measurement-pipeline.md](docs/firmware/measurement-pipeline.md) |
| Understand a specific sensor driver | [docs/firmware/sensors/README.md](docs/firmware/sensors/README.md) |
| Understand the web UI routes | [docs/firmware/web-ui.md](docs/firmware/web-ui.md) |
| Understand upload adapters / HTTP | [docs/firmware/upload-adapters.md](docs/firmware/upload-adapters.md) + [docs/firmware/upload-transport.md](docs/firmware/upload-transport.md) |
