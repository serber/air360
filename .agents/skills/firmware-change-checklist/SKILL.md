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

## Scope

Focus on:

- `firmware/AGENTS.md`
- `docs/firmware/change-impact-map.md`
- `docs/firmware/configuration-reference.md`
- the specific subsystem docs related to the change

## Workflow

1. Identify the primary file or subsystem being changed.
2. Use `docs/firmware/change-impact-map.md` to find likely co-change files.
3. Check whether config, routes, storage, or driver registration are involved.
4. List the docs that must move with the code.
5. Include verification steps: build, doc checker, and any subsystem-specific review.

## Output shape

List:

- code files to inspect or update
- docs that must be reviewed
- validation commands
- remaining hardware or runtime risks
