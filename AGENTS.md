# Air360 project guidance

## Repository structure

- `docs/` contains architecture, analysis, planning, and project context documents.
- `firmware/` contains the actual ESP-IDF firmware project.

## Documentation rules

- Treat `firmware/` as the source of truth for implemented behavior.
- Treat `docs/` as design and planning context unless implementation confirms it.
- Keep repository-level docs and firmware-level docs clearly separated.
- Avoid documenting generated files in `firmware/build/` except when narrow factual hints are needed.
- Preserve project-specific terminology and avoid generic boilerplate.