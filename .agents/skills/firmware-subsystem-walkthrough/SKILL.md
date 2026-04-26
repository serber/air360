---
name: firmware-subsystem-walkthrough
description: "Explain a specific Air360 firmware subsystem with the right reading order, source files, and related docs. Use when mapping boot, networking, sensors, web UI, uploads, or storage for a contributor or agent."
---

# Firmware Subsystem Walkthrough

## When to use this skill

Use this skill when the user asks to:

- explain a firmware subsystem
- map which files own a behavior
- identify where to start reading for a bug or feature
- produce a subsystem-specific reading order for an AI agent

## Scope

Focus on firmware implementation docs and code:

- `firmware/AGENTS.md`
- `docs/firmware/*.md`
- `firmware/main/src/**`
- `firmware/main/include/air360/**`

## Workflow

1. Read the task route in `firmware/AGENTS.md` or `docs/firmware/README.md`.
2. Open the matching subsystem document.
3. Open the code files listed in that document's `Source of truth in code` section.
4. Summarize ownership boundaries, runtime flow, and likely co-change files.
5. Point to the next 2-3 docs the reader should open.

## Output shape

Include:

- what the subsystem is responsible for
- which files are authoritative
- which docs to read first
- what usually changes together
