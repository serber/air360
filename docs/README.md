# Air360 Documentation

Top-level index for all documentation in this repository. For project-level context and repository layout see [`../README.md`](../README.md).

## Areas

| Directory | What it covers |
|-----------|----------------|
| [firmware/README.md](firmware/README.md) | Full navigation map for firmware implementation docs — architecture, subsystems, sensor drivers, ADRs |
| [backend/README.md](backend/README.md) | Backend design notes, ingest API contract, Ubuntu deployment guide |
| [portal/README.md](portal/README.md) | Portal scope, stack direction, boundary with the backend |
| [ecosystem/README.md](ecosystem/README.md) | Sensor.Community compatibility analysis and opportunity roadmap |

## Source-of-truth hierarchy

- `firmware/` — source of truth for implemented device behavior
- `backend/` — source of truth for implemented backend behavior
- `portal/` — source of truth for implemented portal behavior
- `docs/` — explanatory, design context, and planning; not authoritative on its own

When a document here conflicts with the relevant implementation directory, trust the code.
