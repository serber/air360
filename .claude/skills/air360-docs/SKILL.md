---
name: air360-docs
description: "Use this skill when the task is to create, update, audit, or improve repository-level documentation for the Air360 project: the root README.md, docs/README.md, the per-area docs indexes (docs/firmware, docs/backend, docs/portal, docs/guide, docs/hardware), onboarding and navigation docs, and explanations of how docs/ relates to the firmware/, backend/, and portal/ implementations. Do not use for firmware implementation docs (air360-firmware-docs) or for code changes."
---

# Air360 Repository Documentation Skill

## When to use this skill

Use this skill when the user asks to:

- create or rewrite the root `README.md`
- update `docs/README.md` or a per-area index (`docs/<area>/README.md`)
- explain the overall repository structure or produce a documentation map
- write onboarding material for new contributors
- audit repository-level documentation for broken links, stale structure, or
  overstated implementation status

Do not use this skill for firmware implementation docs under `docs/firmware/`
(use `air360-firmware-docs`), for the end-user build guide content under `docs/guide/`
when the request is about device behaviour (that follows firmware changes), or for
backend/portal implementation docs beyond their index pages.

---

## Repository scope

Air360 is a multi-application repository:

| Directory | What it is | Local agent contract |
|-----------|------------|----------------------|
| `firmware/` | ESP-IDF 6.x, C++20 firmware for ESP32-S3 | `firmware/CLAUDE.md` |
| `backend/` | Fastify backend: device registration, ingest, public device list, history | `backend/CLAUDE.md` |
| `portal/` | Next.js public portal | `portal/CLAUDE.md` |
| `docs/` | Documentation, split by area (below) | root `CLAUDE.md` |
| `scripts/` | Repository checkers (`check_firmware_docs.py`, `check_style.py`, `check_firmware_host_tests.py`) | — |
| `.claude/skills/` | Agent skills | root `CLAUDE.md` skills table |

`docs/` areas:

| Directory | Role | Authority |
|-----------|------|-----------|
| `docs/firmware/` | Implementation docs for the firmware: architecture, subsystems, sensors, ADRs | Explanatory; `firmware/` is the source of truth |
| `docs/backend/` | Backend design notes, ingest contract, deployment | Explanatory; `backend/` is the source of truth |
| `docs/portal/` | Portal scope and boundaries | Explanatory; `portal/` is the source of truth |
| `docs/guide/` | End-user build guide: assembly, flashing, web UI, backends, monitoring, troubleshooting | Must follow firmware behaviour |
| `docs/hardware/` | Shield photos, Gerbers, solar module photos | Reference assets |

Each area has its own `README.md` index. `docs/README.md` is the top-level index and
lists the agent entry points. The root `README.md` is the public front page.

---

## Primary inputs to inspect

Always read, in this order:

1. `README.md` (root) — current public structure; preserve its section shape.
2. `docs/README.md` — area table, agent entry points, source-of-truth hierarchy.
3. `docs/<area>/README.md` for every area the task touches.
4. `CLAUDE.md`, `firmware/CLAUDE.md`, `backend/CLAUDE.md`, `portal/CLAUDE.md` — the
   agent contracts must stay consistent with whatever navigation you write.
5. The root `CLAUDE.md` skills table when a skill is added, renamed, or rescoped.

Then verify every path you mention exists (`ls`, `test -f`). Repository docs have
drifted before by naming files that were removed; a documentation map with dead
entries is worse than none.

---

## Source-of-truth rules

- `firmware/`, `backend/`, `portal/` are the source of truth for implemented behaviour
  in their area.
- `docs/firmware/`, `docs/backend/`, `docs/portal/` explain those implementations;
  they are not independent evidence.
- `docs/guide/` describes the device as the firmware implements it; when firmware
  behaviour changes the guide follows.
- Never present a design note or plan as implemented. When status matters, say
  "implemented", "documented but unverified", or "planned" explicitly.

---

## Templates

- `.claude/skills/air360-docs/templates/root-readme.template.md` — structure for a
  root README rewrite. The current `README.md` already follows it; when updating an
  existing README, edit in place instead of regenerating from the template.

Template rules: adapt to the actual tree, never keep placeholder wording, remove
sections that do not apply, keep project terminology (setup AP, measurement pipeline,
upload adapters, power gate, release bundle).

---

## Output types

### Root README

Keep the existing shape: Overview → Repository Layout → Documentation Map →
Components (Firmware / Backend / Portal) → Getting Started → Current Status →
Development Notes. Every Documentation Map entry is a relative link to a file that
exists.

### `docs/README.md` and area indexes

Area table, agent entry points, source-of-truth hierarchy. An area index lists every
document in the directory with a one-line purpose; new docs are added to the index in
the same change that creates them.

### Documentation map / onboarding

Answer, in order: where to start for architecture context; where the end-user build
guide is; where each implementation lives; which `CLAUDE.md` governs the work; how to
verify (checkers, host tests, `validate.sh`).

---

## Audit checklist

1. Every relative link in `README.md`, `docs/README.md`, and each area index resolves.
   `python3 scripts/check_firmware_docs.py` covers `docs/firmware/`; check the rest
   with a quick script or by hand.
2. Every directory in the repository layout section exists; every existing top-level
   directory that matters is mentioned.
3. The skills table in the root `CLAUDE.md` matches `.claude/skills/*/SKILL.md`
   names and descriptions.
4. "Current status" claims are backed by code or by a doc that names the code.
5. Terminology matches the implementation docs (do not rename concepts in the README).

---

## Writing rules

- Markdown, practical headings, contributor-facing tone.
- Prefer short navigation text with exact file names over prose summaries.
- Do not paste ESP-IDF, Fastify, or Next.js tutorial content into repository docs.
- Do not document generated directories (`firmware/build/`, `firmware/release/`,
  `node_modules/`) as maintained structure.

---

## Definition of done

- Structure described matches the tree; all links resolve.
- `docs/`, `firmware/`, `backend/`, `portal/` are distinguished and their authority
  stated.
- Implementation status is not overstated.
- Indexes updated for any document added, moved, or removed.
- Output is commit-ready Markdown.
