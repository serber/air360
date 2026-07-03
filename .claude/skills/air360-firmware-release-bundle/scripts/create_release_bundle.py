#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path
from typing import Iterable


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Create a GitHub-release-ready Air360 firmware bundle from firmware/build outputs."
    )
    parser.add_argument(
        "requested_version",
        help="Requested release version, for example v0.1-beta.1 or v0.1",
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


def normalize_prefix(project_version: str) -> str:
    version = project_version.strip()
    if not version:
        raise SystemExit("Empty project version in build/project_description.json")
    if not version.startswith("v"):
        version = f"v{version}"
    return f"air360-{version}"


def flash_size_slug(flash_size: str) -> str:
    return flash_size.strip().lower()


def choose_esptool_command() -> list[str]:
    candidates: list[list[str]] = []

    idf_python_env = os.environ.get("IDF_PYTHON_ENV_PATH")
    if idf_python_env:
        candidates.append([str(Path(idf_python_env) / "bin" / "python"), "-m", "esptool"])

    candidates.append([str(Path.home() / ".espressif" / "tools" / "python" / "v6.0" / "venv" / "bin" / "python"), "-m", "esptool"])
    candidates.append([sys.executable, "-m", "esptool"])

    seen: set[tuple[str, ...]] = set()
    for candidate in candidates:
        key = tuple(candidate)
        if key in seen:
            continue
        seen.add(key)

        executable = Path(candidate[0])
        if not executable.exists():
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

    esptool_py = shutil.which("esptool.py")
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


def zip_directory(source_dir: Path, zip_path: Path) -> None:
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in sorted(source_dir.rglob("*")):
            if path.is_file():
                archive.write(path, arcname=path.relative_to(source_dir))


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
"""
    path.write_text(notes, encoding="utf-8")


def main() -> int:
    args = parse_args()

    repo_root = repo_root_from_script()
    firmware_dir = (args.firmware_dir or (repo_root / "firmware")).resolve()
    build_dir = firmware_dir / "build"
    release_root = (args.release_dir or (firmware_dir / "release")).resolve()

    project_description_path = build_dir / "project_description.json"
    flasher_args_path = build_dir / "flasher_args.json"
    require_file(project_description_path)
    require_file(flasher_args_path)

    project_description = load_json(project_description_path)
    flasher_args = load_json(flasher_args_path)

    project_version = project_description["project_version"]
    target = flasher_args["extra_esptool_args"]["chip"]
    flash_mode = flasher_args["flash_settings"]["flash_mode"]
    flash_freq = flasher_args["flash_settings"]["flash_freq"]
    flash_size = flasher_args["flash_settings"]["flash_size"]
    flash_files: dict[str, str] = flasher_args["flash_files"]

    bundle_prefix = normalize_prefix(project_version)
    bundle_dir = release_root / bundle_prefix
    full_dir = bundle_dir / "full"
    split_dir = bundle_dir / "split"

    if bundle_dir.exists():
        shutil.rmtree(bundle_dir)
    full_dir.mkdir(parents=True, exist_ok=True)
    split_dir.mkdir(parents=True, exist_ok=True)

    for relpath in flash_files.values():
        source_path = build_dir / relpath
        require_file(source_path)
        shutil.copy2(source_path, split_dir / Path(relpath).name)

    write_flash_offsets(
        split_dir / "flash-offsets.txt",
        target=target,
        flash_mode=flash_mode,
        flash_freq=flash_freq,
        flash_size=flash_size,
        flash_files=flash_files,
    )

    artifact_base = f"{bundle_prefix}-{target}-{flash_size_slug(flash_size)}"
    merged_bin = full_dir / f"{artifact_base}-full.bin"
    full_zip = bundle_dir / f"{artifact_base}-full.zip"
    split_zip = bundle_dir / f"{artifact_base}-split.zip"

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
        raise SystemExit(result.returncode)

    zip_directory(full_dir, full_zip)
    zip_directory(split_dir, split_zip)

    highlights, previous_ref = collect_highlights(repo_root, project_version)
    write_release_notes(
        bundle_dir / "release-notes.md",
        requested_version=args.requested_version,
        highlights=highlights,
        previous_ref=previous_ref,
    )

    checksum_files = [
        merged_bin,
        split_dir / "bootloader.bin",
        split_dir / "partition-table.bin",
        split_dir / "ota_data_initial.bin",
        split_dir / "air360_firmware.bin",
        full_zip,
        split_zip,
    ]
    write_checksums(bundle_dir / "sha256sums.txt", checksum_files, bundle_dir)

    print(f"Created release bundle: {bundle_dir}")
    print(f"Merged image: {merged_bin}")
    print(f"Full zip: {full_zip}")
    print(f"Split zip: {split_zip}")
    print(f"Release notes: {bundle_dir / 'release-notes.md'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
