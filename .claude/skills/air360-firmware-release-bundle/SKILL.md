---
name: air360-firmware-release-bundle
description: Cut an Air360 firmware release in one command — verify the tree on `main`, create the version tag, run a clean ESP-IDF build on that tag, and package `firmware/build` into a GitHub-release-ready bundle under `firmware/release/`. Use when the user asks to release, tag, or package a firmware version (stable or beta). Also covers re-packaging an existing build without tagging.
---

# Air360 Firmware Release Bundle

## Overview

One command turns the current `main` HEAD into a tagged, built, and packaged firmware release:

```bash
python3 .claude/skills/air360-firmware-release-bundle/scripts/release_firmware.py v1.4
```

The script performs, in order, and stops at the first failure:

1. **Preflight** — the version matches `vMAJOR.MINOR[.PATCH][-alpha.N|-beta.N|-rc.N]`; HEAD is on `main`; the tracked tree is clean; after `git fetch origin --tags`, `main` is not behind `origin/main`.
2. **Tag** — creates the annotated tag on HEAD (`Air360 firmware <version>` unless `--message` is given). An existing tag on a different commit is an error; an existing tag on HEAD needs `--reuse-tag`.
3. **Build** — `idf.py fullclean` then `idf.py build`, sourcing `~/.espressif/v6.0/esp-idf/export.sh`. The build must run *after* tagging because ESP-IDF derives `project_version` from `git describe` at CMake configure time, and a new tag does not invalidate an existing CMake cache on its own.
4. **Verify** — `build/project_description.json` carries exactly the requested version (no hash, no `-dirty`).
5. **Bundle** — packages `firmware/build` into `firmware/release/air360-<version>/` (see Output Contract).
6. **Push** (only with `--push`) — `git push origin main` and the tag.

If the build or the bundle fails, a tag created by this run is deleted again, so the fix-and-rerun path starts clean. Existing tags are never deleted.

`firmware/release/` is git-ignored; bundles are never committed.

## When the user asks for a release

1. Confirm the target version with the user if it is not explicit (look at `git tag -l --sort=-v:refname | head` for the last one). Stable tags look like `v1.4`; betas like `v1.5-beta.1`.
2. Make sure the release PR is merged and the user is on `main` with the merged commit pulled. The script enforces this, so do not try to release from `development`.
3. Run the command above from the repo root. Let the build stream; it takes a few minutes.
4. Open `firmware/release/air360-<version>/release-notes.md`, check the highlights, and add testing notes or sign-off by hand.
5. Report the bundle path and the remaining manual steps printed under `==> Done` (push if `--push` was not used, then create the GitHub release and upload the assets). Do not push or create the GitHub release unless the user asked for it.

## Options

| Flag | Use it when |
|------|-------------|
| `--push` | The user wants the branch and tag pushed as part of the release. Off by default: pushing a tag is outward-facing. |
| `--message`, `-m` | A custom annotated-tag message is wanted. |
| `--reuse-tag` | The tag already exists on HEAD (e.g. a previous run pushed the tag but the bundle was lost) and only the build + bundle should be redone. |
| `--no-clean` | A quick re-run after a successful clean build of the same commit; runs `idf.py reconfigure build` instead of a full clean rebuild. Never for the first build of a release. |
| `--skip-build` | `firmware/build` was already built on this exact tag and only packaging is needed. The version check still applies. |
| `--any-branch` | A hotfix or experiment must be tagged from a branch other than `main`. Say so explicitly in the report. |
| `--branch NAME` | The release branch is not `main`. |
| `--no-fetch` | Offline; skips `git fetch` and the behind/ahead check against origin. |
| `--idf-export PATH` | ESP-IDF lives elsewhere (also `AIR360_IDF_EXPORT`). |

## Packaging only

`scripts/create_release_bundle.py <version>` packages an existing `firmware/build` without touching git. `release_firmware.py` calls it internally; run it directly only to re-package a build that was already made on the tag. It refuses a build whose `project_version` differs from the requested version or ends in `-dirty`; `--allow-version-mismatch` and `--allow-dirty` override that for local test bundles, which are then named after the build's own version (for example `air360-v7379ef3-dirty-…`) and must not be published.

## Output Contract

`firmware/release/air360-<version>/` contains:

- `air360-<version>-esp32s3-16mb-full.bin` — merged single image (bootloader + partition table + otadata + app), shipped uncompressed. **Serial flashing only**: write to `0x0` with esptool or espflash.app. Not OTA-flashable — it starts with the bootloader, so OTA fails with `ESP_ERR_OTA_VALIDATE_FAILED`.
- `air360-<version>-esp32s3-16mb-ota.bin` — the application image only. **This is the file for OTA updates.**
- `split/` — `bootloader.bin`, `partition-table.bin`, `ota_data_initial.bin`, `air360_firmware.bin`, and `flash-offsets.txt` (target, flash mode/freq/size, offset per file).
- `air360-<version>-esp32s3-16mb-split.zip` — the `split/` folder; entry timestamps are the release commit's date, so the archive is byte-reproducible for the same build.
- `release-notes.md` — `# Release Notes`, a one-paragraph factual summary, `## Highlights` (non-merge commit subjects between the previous tag and this one; an empty range reads as a stabilization release), and `## Flashing` (which asset is for serial vs OTA). Editorial notes (testing duration, sign-off) are added by hand.
- `sha256sums.txt` — checksums of both `.bin` images, every file in `split/`, and the zip.

The target and flash-size parts of the names come from `build/flasher_args.json`, so they follow the build rather than being hard-coded.

Upload to the GitHub release: the two `.bin` files, the split zip, `sha256sums.txt`, and the release notes text.

## Validation

`release_firmware.py` already fails on a missing build, a version mismatch, a dirty build, or a failed `merge-bin`. Before reporting, still confirm:

- the `==> Done` block printed the bundle path;
- `release-notes.md` names the requested version and the highlights look like this release's commits (if the range is wrong, the previous tag was not reachable from `main`);
- `git tag -l <version>` shows the tag, and `git describe --tags` on `main` returns exactly the version.

## scripts/

- `release_firmware.py` — end-to-end release driver: preflight, tag, clean build, verify, bundle, optional push, tag rollback on failure.
- `create_release_bundle.py` — packaging stage used by the driver; also usable standalone on an existing build.
