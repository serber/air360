---
name: air360-docs
description: "Use this skill when the task is to create, update, audit, or improve repository-level documentation for the Air360 project. Best for root README generation, repository structure explanation, documentation map creation, onboarding docs, architecture summaries based on docs/, and explaining how docs/ and firmware/ relate. Do not use for low-level firmware build or implementation documentation unless the request is primarily about the repository as a whole."
---

# Air360 Repository Documentation Skill

## When to use this skill

Use this skill when the user asks to:

- create or rewrite the root `README.md`
- explain the overall repository structure
- document what is in `docs/`
- document onboarding flow for new contributors
- create a documentation map for the project
- summarize architecture at the repository level
- explain the relationship between `docs/` and `firmware/`
- audit repository documentation for clarity and consistency

Do not use this skill for detailed ESP-IDF build, flash, monitor, or source-module documentation unless the request is clearly about the whole repository.

---

## Repository scope

This repository appears to have two main layers:

- `docs/` — design, analysis, planning, and architecture context
- `firmware/` — the actual ESP-IDF firmware project

Also consider:

- root `AGENTS.md` as project-wide instruction context
- root-level files such as `LICENSE` and `.gitignore` as repository metadata

This skill is responsible for describing the repository as a whole, not the internal firmware implementation in detail.

---

## Main goals

When this skill is used, produce documentation that helps a developer quickly understand:

- what this repository is for
- where to start reading
- which files describe architecture and planning
- where the buildable firmware lives
- how design documents relate to implementation
- what appears implemented vs planned

---

## Primary inputs to inspect

Check these first:

- `AGENTS.md`
- `docs/`
- `firmware/README.md`
- root-level repository structure

Priority files in `docs/` include:

- `docs/modern-replacement-firmware-architecture.md`
- `docs/firmware-iterative-implementation-plan.md`
- `docs/phase-1-hardware-boot-notes.md`
- `docs/airrohr-firmware-server-contract.md`
- `docs/airrohr-firmware-ui-analysis.md`

Use these documents to understand:

- project intent
- design direction
- architecture vocabulary
- hardware notes
- backend or UI assumptions

Do not assume design docs are proof of implementation.

---

## Templates

Preferred template for new repository-level documentation:

- `templates/root-readme.template.md`

When generating a new root README, repository overview, or onboarding document, start from this template and adapt it to the actual repository contents instead of copying it verbatim.

Template rules:

- keep repository-level scope
- explain `docs/` and `firmware/` separately
- preserve project-specific terminology
- remove irrelevant sections
- replace all placeholder wording with repository-specific content

---

## Required reasoning rules

### Separate repository truth from implementation truth

At repository level:

- `docs/` may describe intended design, analysis, or roadmap
- `firmware/` contains implementation evidence

When writing repository docs:

- explain both layers clearly
- do not merge them into one undifferentiated description
- distinguish:
  - architectural intent
  - implementation status
  - open work or planned work

### Preserve project terminology

Reuse naming already present in the repository where possible.

### Avoid generic filler

Do not write generic ESP-IDF tutorial content in root documentation unless needed for navigation.

---

## Default output types

### 1. Root README

Use this structure by default:

# Air360

## Overview
What this repository contains and the project goal.

## Repository layout
Explain the purpose of:
- `docs/`
- `firmware/`
- `AGENTS.md`

## Documentation map
List the major docs in `docs/` and what each one is for.

## Firmware
Explain that the buildable ESP-IDF application lives in `firmware/`.

## Getting started
Tell a developer where to begin depending on whether they need:
- architecture context
- hardware notes
- implementation/build instructions

## Current status
Summarize what appears implemented versus planned.

## Development notes
Contributor-facing guidance.

### 2. Documentation map

When the user asks for project navigation or onboarding, provide a concise map of:

- which file to read first
- which document explains architecture
- which document explains hardware
- which document explains firmware plans
- where the implementation lives

### 3. Repository onboarding doc

For new contributors, explain:

- where to start
- what docs are authoritative for design
- where current implementation lives
- how to avoid confusing plans with implemented features

---

## Writing rules

### Accuracy

- Do not claim a feature is implemented unless implementation confirms it.
- Do not claim repository structure that is not present.
- Prefer concise summaries backed by actual file names.

### Style

- Write in Markdown.
- Use practical headings.
- Keep text oriented toward maintainers and contributors.
- Prefer clarity over completeness when documenting top-level structure.

### Framing

Useful phrasing patterns:

- "The repository contains..."
- "The `docs/` folder appears to capture design and planning context..."
- "The `firmware/` directory contains the ESP-IDF application..."
- "Based on the current structure..."
- "Implementation should be verified against firmware sources..."

---

## Good behavior for common requests

### If asked to "create README"

Prefer a repository-level README if the request mentions the whole repo, `docs/`, onboarding, architecture overview, or project structure.

### If asked to "document the project"

Cover:

- repo structure
- key docs
- firmware location
- current status boundaries

### If asked to "audit docs"

Check whether current repository documentation:

- explains the purpose of `docs/`
- points clearly to `firmware/`
- distinguishes design docs from implemented behavior

---

## Working style

- Keep repository-level scope. Do not drift into detailed firmware implementation docs unless the request is explicitly about repo-level navigation to them.
- Treat `docs/` as planning, architecture, and project context by default.
- Treat `docs/firmware/` as implementation-oriented firmware documentation when it exists.
- Treat `firmware/` as the implementation source of truth.
- Prefer short, accurate navigation documents over broad generic summaries.

---

## Definition of done

A repository documentation task is complete only if:

- the repository structure is explained clearly
- `docs/` and `firmware/` are distinguished correctly
- important documents in `docs/` are mapped to their purpose
- implementation is not overstated
- the output is ready to commit as Markdown

---

## Anti-patterns to avoid

Do not:

- treat planning docs as implementation truth
- dump ESP-IDF setup instructions into root docs unless needed for orientation
- document `firmware/build/` as part of the maintained project structure
- invent hardware details not confirmed anywhere
- mix repository-level and firmware-level guidance without labeling the scope

---

## Example trigger phrases

Activate this skill for requests like:

- "Create a root README for this repository"
- "Explain the structure of this Air360 repo"
- "Document how docs/ and firmware/ relate"
- "Create onboarding docs for new contributors"
- "Summarize the project architecture from the repository docs"
- "Audit the repository documentation"
