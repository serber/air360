---
name: firmware-change-checklist
description: "Produce an Air360 firmware change checklist before or after editing code. Use when a task needs a concrete list of co-change files, docs to update, and verification steps."
---

# Firmware Change Checklist

## When to use this skill

Use this skill when the user asks to:

- plan a firmware change safely
- identify what else must be updated for a code change
- generate a verification checklist for firmware work
- avoid missing docs after a firmware refactor or feature change

## Inputs

- `firmware/CLAUDE.md` — co-change expectations and the verification checklist
- `docs/firmware/change-impact-map.md` — code → code → docs matrix
- `.claude/skills/esp-idf-cpp-developer/references/recipes.md` — step lists for the
  recurring change types (config field + form, boot step, sensor descriptor field,
  status surfacing, new task, Kconfig tunable)
- `.claude/skills/esp-idf-cpp-developer/references/air360-map.md` — concern → code → doc
- the subsystem doc for the area (table in `firmware/CLAUDE.md`)

## Workflow

1. Name the primary file or subsystem being changed.
2. Look the file up in `change-impact-map.md`; list the co-change code files.
3. If the change matches a recipe, take the file list from `recipes.md` instead of
   deriving it by hand.
4. Check the usual hidden dependencies: persisted struct layout (blob reset on size
   change), web form pipeline, status JSON/Overview, boot-step numbering, sensor
   registry descriptors, log-tag registry in `firmware/CLAUDE.md`.
5. List the docs that must move with the code: `docs/firmware/*.md` per the map, and
   `docs/guide/*.md` whenever the change is visible to a device user (web UI, LED,
   boot behaviour, configuration fields).
6. List verification steps.

## Verification steps to include

```bash
bash .claude/skills/esp-idf-cpp-developer/scripts/validate.sh   # build + style + docs + host tests
```

or the individual pieces: canonical build from `firmware/`, `python3 scripts/check_style.py`
(from `firmware/`), `python3 scripts/check_firmware_docs.py`,
`python3 scripts/check_firmware_host_tests.py`. Add the hardware checks the change
needs (serial log lines to look for, web pages to open, behaviours to provoke).

## Output shape

- code files to inspect or update (primary + co-change)
- docs that must be reviewed (`docs/firmware/` and `docs/guide/`)
- validation commands
- remaining hardware or runtime risks and what cannot be verified off-device
