---
name: air360-firmware-release-bundle
description: Create a GitHub-release-ready firmware bundle for the Air360 ESP-IDF project from the current `firmware/build` outputs. Use when preparing a beta or stable firmware release and you need a deterministic package under `firmware/release/` with a versioned bundle folder, an uncompressed merged `full` image for serial flashing, an `ota` application image for OTA updates, `split` images, release-note markdown, and a split zip archive.
---

# Air360 Firmware Release Bundle

## Overview

Create a deterministic release bundle for the current Air360 firmware build. The skill packages the already-built ESP-IDF artifacts from `firmware/build` into a versioned folder under `firmware/release/`: a merged image for serial flashing, the application image for OTA updates, the split images (zipped), and release notes.

## Preconditions

File and folder names come from `project_version` in `build/project_description.json`, which ESP-IDF derives from `git describe` at **build time**. Highlights in the release notes are the commits between the previous tag and the tag matching that version. So the order matters:

1. Commit everything; the working tree must be clean (a dirty tree adds `-dirty` to the version).
2. Create the tag on the release commit: `git tag -a v0.1-beta.1 -m "..."`.
3. Build **on that tag commit** with the canonical command from `firmware/CLAUDE.md`:
   `source ~/.espressif/v6.0/esp-idf/export.sh && idf.py build` (run from `firmware/`).
4. Only then run the bundle script. A bundle built before tagging is named after the previous tag plus a commit suffix and its notes fall into the wrong range.

`firmware/release/` is git-ignored; bundles are never committed.

## Workflow

1. Confirm the preconditions above and that `firmware/build/` contains fresh `.bin` outputs for the tag commit.
2. Run `scripts/create_release_bundle.py <requested-version>` from the repo root.
3. Review the generated folder under `firmware/release/air360-v<version>/`; open `release-notes.md` and add testing notes or sign-off by hand.
4. Upload the generated zip files, the `-full.bin`, the `-ota.bin`, `sha256sums.txt`, and the release notes to GitHub Releases.

## Output Contract

The script creates:

- `firmware/release/air360-v<project_version>/`
- `air360-v194e0b6-esp32s3-16mb-full.bin` — merged single image at the bundle root, shipped uncompressed. **Serial flashing only** (write to `0x0` with esptool). It starts with the bootloader, so it is **not** OTA-flashable — pushing it over OTA fails with `ESP_ERR_OTA_VALIDATE_FAILED`.
- `air360-v194e0b6-esp32s3-16mb-ota.bin` — the application image only, copied from the build's app artifact. **This is the file to use for OTA updates.**
- `split/` with `bootloader.bin`, `partition-table.bin`, `ota_data_initial.bin`, `air360_firmware.bin`, and `flash-offsets.txt`
- `air360-v194e0b6-esp32s3-16mb-split.zip`
- `release-notes.md`
- `sha256sums.txt`

The requested version string such as `v0.1-beta.1` or `v0.1` is used inside `release-notes.md`. File and bundle names are derived from the build commit-style project version from `build/project_description.json`.

`release-notes.md` follows a fixed template: a top-level `# Release Notes` heading, a one-paragraph factual summary, a `## Highlights` section, and a `## Flashing` section that explains which asset to use for serial flashing vs OTA. Highlights are the non-merge commit subjects between the previous tag and the release ref (the tag matching the build's `project_version` when it exists, otherwise `HEAD`). When the range is empty, the notes read as a stabilization release. The summary states only what is verifiable from git — add any editorial notes (testing duration, sign-off) by hand before publishing.

## Command

Run from the repository root (`air360/`):

```bash
python3 .claude/skills/air360-firmware-release-bundle/scripts/create_release_bundle.py v0.1-beta.1
```

Optional overrides:

```bash
python3 .claude/skills/air360-firmware-release-bundle/scripts/create_release_bundle.py v0.1 \
  --firmware-dir /path/to/repo/firmware \
  --release-dir /path/to/repo/firmware/release
```

## Validation

Before trusting the bundle, confirm that:

- `build/flasher_args.json` exists
- `build/project_description.json` exists
- the four required split binaries exist
- the merged `-full.bin` was created at the bundle root
- the `-ota.bin` application image was created at the bundle root
- the split zip archive was created
- `release-notes.md` mentions the requested version

## scripts/

- `create_release_bundle.py`
  Deterministically creates the versioned release bundle from the current firmware build outputs.
