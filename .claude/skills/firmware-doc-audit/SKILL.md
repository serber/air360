---
name: firmware-doc-audit
description: "Audit Air360 firmware docs against the current implementation. Use when checking whether docs/firmware, firmware/README.md, docs/guide, and firmware-local agent docs still match the code."
---

# Firmware Doc Audit

## When to use this skill

Use this skill when the user asks to:

- audit firmware documentation for accuracy
- identify stale or missing firmware docs
- improve agent-readability of firmware docs
- check whether a recent firmware change updated the right docs

## Scope

- `firmware/` (source of truth), `firmware/CLAUDE.md`, `firmware/README.md`
- `docs/firmware/` — implementation docs, ADRs, sensor pages
- `docs/guide/` — end-user guide; it must describe the device as the firmware behaves
- `.claude/skills/esp-idf-cpp-developer/references/*.md` — the developer skill's map
  and recipes drift like docs do

## Workflow

1. Read `CLAUDE.md` and `firmware/CLAUDE.md`.
2. Read `docs/firmware/README.md` and `docs/firmware/change-impact-map.md`.
3. Build the source inventory first (the table in
   `.claude/skills/air360-firmware-docs/SKILL.md` → "Firmware doc audit" lists the
   commands: source files, routes, NVS structs, sensor types, Kconfig, tasks, boot
   steps, managed components, host tests).
4. Compare inventory → docs, never docs → memory. For a recent change, start from
   `git diff --stat <base>` and check each touched file against the impact map.
5. Check the cross-cutting facts that break silently: boot-step numbering `N/M`
   across code and `startup-pipeline.md` / `ARCHITECTURE.md`; runtime state enums
   listed in `ARCHITECTURE.md` and `web-ui.md`; constants tables in subsystem docs;
   the log-tag registry in `firmware/CLAUDE.md`; the docs index and `Read next`
   links for any new document.
6. Run `python3 scripts/check_firmware_docs.py`.

## Audit outputs

Prioritize:

- broken links and missing index entries
- facts that contradict the code (numbers, names, order, defaults)
- missing source-of-truth references or stale file paths
- undocumented co-change requirements
- user-guide sections that describe behaviour the firmware no longer has

## Writing rule

Prefer code-to-doc comparison, not doc-to-memory comparison. Fix only what is stale;
do not rewrite accurate sections.
