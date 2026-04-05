---
name: air360-firmware-release-bundle
description: Create a GitHub-release-ready firmware bundle for the Air360 ESP-IDF project from the current `firmware/build` outputs. Use when preparing a beta or stable firmware release and you need a deterministic package under `firmware/release/` with a versioned bundle folder, merged `full` image, `split` images, release-note markdown, and `full/split` zip archives.
---

# Air360 Firmware Release Bundle

## Overview

Create a deterministic release bundle for the current Air360 firmware build. The skill packages the already-built ESP-IDF artifacts from `firmware/build` into a versioned folder under `firmware/release/`, generates a merged image, copies split images, creates `full/split` zip archives, and writes release notes.

## Workflow

1. Make sure the firmware has already been built and `firmware/build/` contains valid `.bin` outputs.
2. Run `scripts/create_release_bundle.py <requested-version>`.
3. Review the generated folder under `firmware/release/air360-v<commit>/`.
4. Upload the generated zip files and release notes to GitHub Releases.

## Output Contract

The script creates:

- `firmware/release/air360-v<project_version>/`
- `full/` with a merged image named like `air360-v194e0b6-esp32s3-16mb-full.bin`
- `split/` with `bootloader.bin`, `partition-table.bin`, `ota_data_initial.bin`, `air360_firmware.bin`, and `flash-offsets.txt`
- `air360-v194e0b6-esp32s3-16mb-full.zip`
- `air360-v194e0b6-esp32s3-16mb-split.zip`
- `release-notes.md`
- `sha256sums.txt`

The requested version string such as `v0.1-beta.1` or `v0.1` is used inside `release-notes.md`. File and bundle names are derived from the build commit-style project version from `build/project_description.json`.

## Command

Run:

```bash
python3 .agents/skills/air360-firmware-release-bundle/scripts/create_release_bundle.py v0.1-beta.1
```

Optional overrides:

```bash
python3 .agents/skills/air360-firmware-release-bundle/scripts/create_release_bundle.py v0.1 \
  --firmware-dir /path/to/repo/firmware \
  --release-dir /path/to/repo/firmware/release
```

## Validation

Before trusting the bundle, confirm that:

- `build/flasher_args.json` exists
- `build/project_description.json` exists
- the four required split binaries exist
- both zip archives were created
- `release-notes.md` mentions the requested version

## scripts/

- `create_release_bundle.py`
  Deterministically creates the versioned release bundle from the current firmware build outputs.
