---
name: firmware-doc-audit
description: "Audit Air360 firmware docs against the current implementation. Use when checking whether docs/firmware, firmware/README.md, and firmware-local agent docs still match the code."
---

# Firmware Doc Audit

## When to use this skill

Use this skill when the user asks to:

- audit firmware documentation for accuracy
- identify stale or missing firmware docs
- improve agent-readability of firmware docs
- check whether a recent firmware change updated the right docs

## Scope

Focus on:

- `firmware/`
- `firmware/AGENTS.md`
- `firmware/README.md`
- `docs/firmware/`

Treat `firmware/` as the implementation source of truth.

## Workflow

1. Read `AGENTS.md` and `firmware/AGENTS.md`.
2. Read `docs/firmware/README.md` and `docs/firmware/change-impact-map.md`.
3. Inventory the affected source files under `firmware/main/`.
4. Check whether the matching docs point at the right code and describe current behavior.
5. Run `python3 scripts/check_firmware_docs.py`.

## Audit outputs

Prioritize:

- broken links
- missing agent entry points
- missing or stale source-of-truth references
- undocumented co-change requirements
- missing sensor or ADR index entries

## Writing rule

Prefer code-to-doc comparison, not doc-to-memory comparison.
