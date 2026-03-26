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
