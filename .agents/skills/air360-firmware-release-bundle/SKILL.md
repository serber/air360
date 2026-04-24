---
name: air360-firmware-release-bundle
description: Create a GitHub-release-ready firmware bundle for the Air360 ESP-IDF project from current `firmware/build` outputs and git release tags. Use when preparing a beta or stable firmware release and you need a deterministic package under `firmware/release/` with a tag-versioned bundle folder, merged `full` image, `split` images, auto-generated markdown release notes from git changes since the previous comparable tag, and `full/split` zip archives.
---

# Air360 Firmware Release Bundle

## Overview

Create a deterministic release bundle for the current Air360 firmware build. The script packages already-built ESP-IDF artifacts from `firmware/build`, derives the release version from the latest git tag, finds the previous tag with the same trailing-number naming convention, generates markdown release notes from `git log previous..latest`, creates merged/split images, and writes zip archives plus checksums.

## Workflow

1. Make sure the firmware has already been built and `firmware/build/` contains valid `.bin` outputs.
2. Make sure the intended release tag exists in git. By default the latest version-sorted tag is used, for example `v0.1-beta.5`.
3. Run `scripts/create_release_bundle.py` from the repository root.
4. Review the generated folder under `firmware/release/air360-<latest-tag>/`.
5. Upload the generated zip files and `release-notes.md` to GitHub Releases.

For a tag like `v0.1-beta.5`, the script searches existing tags for the highest lower numeric suffix with the same prefix, for example `v0.1-beta.4` if it exists or `v0.1-beta.3` if `.4` is absent. Release notes are generated from that range.

## Output Contract

The script creates:

- `firmware/release/air360-<release_tag>/`
- `full/` with a merged image named like `air360-v0.1-beta.5-esp32s3-16mb-full.bin`
- `split/` with `bootloader.bin`, `partition-table.bin`, `ota_data_initial.bin`, `air360_firmware.bin`, and `flash-offsets.txt`
- `air360-v0.1-beta.5-esp32s3-16mb-full.zip`
- `air360-v0.1-beta.5-esp32s3-16mb-split.zip`
- `release-notes.md`
- `sha256sums.txt`

The release tag is used in the bundle folder name, binary name, zip names, and `release-notes.md`. The build commit-style project version from `build/project_description.json` is still included inside `release-notes.md` for traceability.

## Command

Run:

```bash
python3 .agents/skills/air360-firmware-release-bundle/scripts/create_release_bundle.py
```

Optional overrides:

```bash
python3 .agents/skills/air360-firmware-release-bundle/scripts/create_release_bundle.py v0.1-beta.5 \
  --firmware-dir /path/to/repo/firmware \
  --release-dir /path/to/repo/firmware/release
```

## Validation

Before trusting the bundle, confirm that:

- `build/flasher_args.json` exists
- `build/project_description.json` exists
- latest git tag or requested tag exists
- previous comparable tag is printed, or the release notes explicitly say none was found
- the four required split binaries exist
- both zip archives were created
- `release-notes.md` mentions the release tag and git change range

## scripts/

- `create_release_bundle.py`
  Deterministically creates the versioned release bundle from the current firmware build outputs.
