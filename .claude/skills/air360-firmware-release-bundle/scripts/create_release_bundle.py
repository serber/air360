#!/usr/bin/env python3
"""Package the current firmware/build outputs into a versioned release bundle.

This is the packaging stage only. It expects that the firmware was built while
HEAD sat exactly on the release tag with a clean tree, so that ESP-IDF's
``git describe`` produced the release version. ``release_firmware.py`` drives
tagging, building, and this script end to end; run this file directly only to
re-package an existing build.
"""
from __future__ import annotations

import argparse
import glob
import hashlib
import json
import os
import shutil
import subprocess
import sys
import zipfile
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Create a GitHub-release-ready Air360 firmware bundle from firmware/build outputs."
    )
    parser.add_argument(
        "requested_version",
        help="Release version, for example v1.4 or v1.4-beta.1. Must match the build's project_version.",
    )
    parser.add_argument(
        "--firmware-dir",
        type=Path,
        default=None,
        help="Path to the firmware project root. Defaults to the repository firmware/ directory.",
    )
    parser.add_argument(
        "--release-dir",
        type=Path,
        default=None,
        help="Path to the release output directory. Defaults to <firmware-dir>/release.",
    )
    parser.add_argument(
        "--allow-dirty",
        action="store_true",
        help="Package a build whose project_version carries a -dirty suffix (never for a published release).",
    )
    parser.add_argument(
        "--allow-version-mismatch",
        action="store_true",
        help="Package even if the build's project_version differs from requested_version "
        "(the files are then named after the build version, not the requested one).",
    )
    return parser.parse_args()


def repo_root_from_script() -> Path:
    # Script lives at .claude/skills/air360-firmware-release-bundle/scripts/
    # parents[0] = scripts/
    # parents[1] = air360-firmware-release-bundle/
    # parents[2] = skills/
    # parents[3] = .claude/
    # parents[4] = air360/ (repo root)
    return Path(__file__).resolve().parents[4]


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def require_file(path: Path) -> None:
    if not path.is_file():
        raise SystemExit(f"Required file not found: {path}")


def git_output(repo_root: Path, args: list[str]) -> str | None:
    try:
        result = subprocess.run(
            ["git", "-C", str(repo_root), *args],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
        )
    except OSError:
        return None
    if result.returncode != 0:
        return None
    return result.stdout.strip()


def normalize_version(version: str) -> str:
    """Return the version with a single leading ``v`` (``1.4`` -> ``v1.4``)."""
    version = version.strip()
    if not version:
        raise SystemExit("Empty version string")
    if not version.startswith("v"):
        version = f"v{version}"
    return version


def collect_highlights(repo_root: Path, project_version: str) -> tuple[list[str], str | None]:
    """Return (highlights, previous_ref) from commit history.

    Highlights are the non-merge commit subjects between the previous tag and the
    release ref. The release ref is the tag matching project_version when it
    exists (so notes are stable regardless of the current checkout), otherwise
    HEAD.
    """
    tagged = git_output(
        repo_root, ["rev-parse", "--verify", "--quiet", f"refs/tags/{project_version}"]
    )
    current_ref = project_version if tagged else "HEAD"
    previous_ref = git_output(repo_root, ["describe", "--tags", "--abbrev=0", f"{current_ref}^"])
    rev_range = f"{previous_ref}..{current_ref}" if previous_ref else current_ref
    log = git_output(repo_root, ["log", "--no-merges", "--pretty=%s", rev_range])
    if not log:
        return [], previous_ref
    highlights = [line.strip() for line in log.splitlines() if line.strip()]
    return highlights, previous_ref


def release_timestamp(repo_root: Path, project_version: str) -> tuple[int, int, int, int, int, int]:
    """Zip entry timestamp: the release commit's author date, so archives are reproducible.

    Falls back to the zip epoch when git is unavailable.
    """
    import datetime

    ref = project_version if git_output(
        repo_root, ["rev-parse", "--verify", "--quiet", f"refs/tags/{project_version}"]
    ) else "HEAD"
    raw = git_output(repo_root, ["log", "-1", "--format=%ct", ref])
    if raw and raw.isdigit():
        stamp = datetime.datetime.fromtimestamp(int(raw), tz=datetime.timezone.utc)
        if stamp.year >= 1980:
            return (stamp.year, stamp.month, stamp.day, stamp.hour, stamp.minute, stamp.second)
    return (1980, 1, 1, 0, 0, 0)


def flash_size_slug(flash_size: str) -> str:
    return flash_size.strip().lower()


def resolve_app_image(flasher_args: dict, project_description: dict) -> str:
    """Return the build-relative path of the application image.

    This is the only OTA-flashable artifact: the merged full image starts with
    the bootloader at 0x0, so it fails esp_ota validation. Prefer the explicit
    ``app`` entry in flasher_args.json; fall back to the flash file whose base
    name matches the project name.
    """
    app = flasher_args.get("app")
    if isinstance(app, dict) and app.get("file"):
        return app["file"]
    project_name = project_description.get("project_name")
    if project_name:
        candidate = f"{project_name}.bin"
        for relpath in flasher_args["flash_files"].values():
            if Path(relpath).name == candidate:
                return relpath
    raise SystemExit("Could not identify the application image for OTA in flasher_args.json")


def choose_esptool_command() -> list[str]:
    """Find a Python with esptool: the active IDF env, any installed IDF env, then the host Python."""
    candidates: list[list[str]] = []

    idf_python_env = os.environ.get("IDF_PYTHON_ENV_PATH")
    if idf_python_env:
        candidates.append([str(Path(idf_python_env) / "bin" / "python"), "-m", "esptool"])

    espressif_home = Path(os.environ.get("IDF_TOOLS_PATH", str(Path.home() / ".espressif")))
    for env_python in sorted(glob.glob(str(espressif_home / "python_env" / "*" / "bin" / "python")), reverse=True):
        candidates.append([env_python, "-m", "esptool"])
    for env_python in sorted(glob.glob(str(espressif_home / "tools" / "python" / "*" / "venv" / "bin" / "python")), reverse=True):
        candidates.append([env_python, "-m", "esptool"])

    candidates.append([sys.executable, "-m", "esptool"])

    seen: set[tuple[str, ...]] = set()
    for candidate in candidates:
        key = tuple(candidate)
        if key in seen:
            continue
        seen.add(key)

        if not Path(candidate[0]).exists():
            continue

        try:
            result = subprocess.run(
                candidate + ["version"],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                check=False,
            )
        except OSError:
            continue

        if result.returncode == 0:
            return candidate

    esptool_py = shutil.which("esptool.py") or shutil.which("esptool")
    if esptool_py:
        return [esptool_py]

    raise SystemExit("Could not find a working esptool command. Load the ESP-IDF environment or install esptool.")


def write_flash_offsets(
    path: Path,
    target: str,
    flash_mode: str,
    flash_freq: str,
    flash_size: str,
    flash_files: dict[str, str],
) -> None:
    lines = [
        f"target: {target}",
        f"flash mode: {flash_mode}",
        f"flash freq: {flash_freq}",
        f"flash size: {flash_size}",
        "",
    ]
    for offset, relpath in flash_files.items():
        lines.append(f"{offset:<8} {Path(relpath).name}")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def zip_directory(source_dir: Path, zip_path: Path, timestamp: tuple[int, int, int, int, int, int]) -> None:
    """Zip a directory with fixed entry timestamps so the archive is byte-reproducible."""
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in sorted(source_dir.rglob("*")):
            if not path.is_file():
                continue
            info = zipfile.ZipInfo(str(path.relative_to(source_dir)), date_time=timestamp)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            archive.writestr(info, path.read_bytes())


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_checksums(path: Path, files: Iterable[Path], bundle_dir: Path) -> None:
    lines = []
    for file_path in files:
        lines.append(f"{sha256(file_path)}  {file_path.relative_to(bundle_dir)}")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def format_highlights(highlights: list[str]) -> str:
    if not highlights:
        return "- No functional code changes - this is a stabilization release."
    return "\n".join(f"- {item}" for item in highlights)


def build_summary(requested_version: str, highlights: list[str], previous_ref: str | None) -> str:
    """A one-paragraph factual intro. Adapts to whether the release has changes.

    Kept intentionally neutral: it states what is verifiable from git and leaves
    editorial claims (test duration, sign-off) for a human to add.
    """
    if not highlights:
        if previous_ref:
            return (
                f"This release promotes `{previous_ref}` to `{requested_version}` with no "
                f"functional firmware changes: it is the same firmware, published as a "
                f"stable release."
            )
        return f"This release publishes Air360 firmware `{requested_version}`."
    change_word = "change" if len(highlights) == 1 else "changes"
    since = f" since `{previous_ref}`" if previous_ref else ""
    return (
        f"This release publishes Air360 firmware `{requested_version}`, bundling "
        f"{len(highlights)} {change_word}{since}. See the highlights below."
    )


def write_release_notes(
    path: Path,
    requested_version: str,
    highlights: list[str],
    previous_ref: str | None,
) -> None:
    notes = f"""# Release Notes

{build_summary(requested_version, highlights, previous_ref)}

## Highlights

{format_highlights(highlights)}

## Flashing

Pick the asset that matches how you flash:

- **`*-full.bin`** — complete image (bootloader + partition table + app). Flash to offset `0x0` over USB/serial with esptool. Do **not** use this for OTA: it starts with the bootloader, so an OTA update fails with `ESP_ERR_OTA_VALIDATE_FAILED`.
- **`*-ota.bin`** — application image only. Use this for over-the-air (OTA) updates.
- **`*-split.zip`** — the individual bootloader / partition-table / ota_data / app binaries with their flash offsets, for manual or advanced serial flashing.
"""
    path.write_text(notes, encoding="utf-8")


@dataclass
class BundleResult:
    bundle_dir: Path
    merged_bin: Path
    ota_bin: Path
    split_zip: Path
    release_notes: Path
    checksums: Path
    project_version: str


def create_bundle(
    requested_version: str,
    *,
    repo_root: Path,
    firmware_dir: Path,
    release_root: Path,
    allow_dirty: bool = False,
    allow_version_mismatch: bool = False,
) -> BundleResult:
    build_dir = firmware_dir / "build"
    project_description_path = build_dir / "project_description.json"
    flasher_args_path = build_dir / "flasher_args.json"
    require_file(project_description_path)
    require_file(flasher_args_path)

    project_description = load_json(project_description_path)
    flasher_args = load_json(flasher_args_path)

    project_version = normalize_version(project_description["project_version"])
    requested_version = normalize_version(requested_version)

    if project_version.endswith("-dirty") and not allow_dirty:
        raise SystemExit(
            f"Build project_version is '{project_version}': the firmware was built from a dirty tree. "
            "Commit or stash, rebuild on the tag, and rerun (or pass --allow-dirty for a local test bundle)."
        )
    if project_version != requested_version and not allow_version_mismatch:
        raise SystemExit(
            f"Build project_version '{project_version}' does not match requested '{requested_version}'. "
            "Tag the release commit and rebuild so ESP-IDF embeds the tag, or pass --allow-version-mismatch "
            "to package the build under its own version."
        )

    target = flasher_args["extra_esptool_args"]["chip"]
    flash_mode = flasher_args["flash_settings"]["flash_mode"]
    flash_freq = flasher_args["flash_settings"]["flash_freq"]
    flash_size = flasher_args["flash_settings"]["flash_size"]
    flash_files: dict[str, str] = flasher_args["flash_files"]

    bundle_prefix = f"air360-{project_version}"
    bundle_dir = release_root / bundle_prefix
    split_dir = bundle_dir / "split"

    if bundle_dir.exists():
        shutil.rmtree(bundle_dir)
    split_dir.mkdir(parents=True, exist_ok=True)

    split_files: list[Path] = []
    for relpath in flash_files.values():
        source_path = build_dir / relpath
        require_file(source_path)
        destination = split_dir / Path(relpath).name
        shutil.copy2(source_path, destination)
        split_files.append(destination)

    write_flash_offsets(
        split_dir / "flash-offsets.txt",
        target=target,
        flash_mode=flash_mode,
        flash_freq=flash_freq,
        flash_size=flash_size,
        flash_files=flash_files,
    )

    artifact_base = f"{bundle_prefix}-{target}-{flash_size_slug(flash_size)}"
    # The merged full image is a single flashable file, so it ships uncompressed
    # at the bundle root; only the multi-file split set is zipped.
    merged_bin = bundle_dir / f"{artifact_base}-full.bin"
    split_zip = bundle_dir / f"{artifact_base}-split.zip"

    # The application image is the only OTA-flashable artifact. Copy it to the
    # bundle root under an explicit -ota name so it is not confused with the
    # merged -full image (which fails esp_ota validation).
    app_source = build_dir / resolve_app_image(flasher_args, project_description)
    require_file(app_source)
    ota_bin = bundle_dir / f"{artifact_base}-ota.bin"
    shutil.copy2(app_source, ota_bin)

    esptool_command = choose_esptool_command()
    merge_command = esptool_command + [
        "--chip",
        target,
        "merge-bin",
        "-o",
        str(merged_bin),
        "--flash-mode",
        flash_mode,
        "--flash-freq",
        flash_freq,
        "--flash-size",
        flash_size,
    ]
    for offset, relpath in flash_files.items():
        merge_command.extend([offset, str(build_dir / relpath)])

    result = subprocess.run(merge_command, check=False)
    if result.returncode != 0:
        raise SystemExit(f"esptool merge-bin failed with exit code {result.returncode}")
    require_file(merged_bin)

    zip_directory(split_dir, split_zip, release_timestamp(repo_root, project_version))

    highlights, previous_ref = collect_highlights(repo_root, project_version)
    release_notes = bundle_dir / "release-notes.md"
    write_release_notes(
        release_notes,
        requested_version=requested_version,
        highlights=highlights,
        previous_ref=previous_ref,
    )

    checksums = bundle_dir / "sha256sums.txt"
    write_checksums(checksums, [merged_bin, ota_bin, *split_files, split_zip], bundle_dir)

    return BundleResult(
        bundle_dir=bundle_dir,
        merged_bin=merged_bin,
        ota_bin=ota_bin,
        split_zip=split_zip,
        release_notes=release_notes,
        checksums=checksums,
        project_version=project_version,
    )


def print_bundle_summary(result: BundleResult) -> None:
    print(f"Created release bundle: {result.bundle_dir}")
    print(f"Merged image (serial flash to 0x0): {result.merged_bin}")
    print(f"OTA image (app only): {result.ota_bin}")
    print(f"Split zip: {result.split_zip}")
    print(f"Release notes: {result.release_notes}")
    print(f"Checksums: {result.checksums}")


def main() -> int:
    args = parse_args()

    repo_root = repo_root_from_script()
    firmware_dir = (args.firmware_dir or (repo_root / "firmware")).resolve()
    release_root = (args.release_dir or (firmware_dir / "release")).resolve()

    result = create_bundle(
        args.requested_version,
        repo_root=repo_root,
        firmware_dir=firmware_dir,
        release_root=release_root,
        allow_dirty=args.allow_dirty,
        allow_version_mismatch=args.allow_version_mismatch,
    )
    print_bundle_summary(result)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
