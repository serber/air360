#!/usr/bin/env python3
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import os
import re
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
        nargs="?",
        help="Optional release tag override. Defaults to the latest git tag.",
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
    return Path(__file__).resolve().parents[4]


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def require_file(path: Path) -> None:
    if not path.is_file():
        raise SystemExit(f"Required file not found: {path}")


def normalize_prefix(project_version: str) -> str:
    version = project_version.strip()
    if not version:
        raise SystemExit("Empty project version in build/project_description.json")
    if not version.startswith("v"):
        version = f"v{version}"
    return f"air360-{version}"


@dataclass(frozen=True)
class GitChange:
    commit: str
    subject: str
    author: str
    date: str
    files: tuple[str, ...]


def run_git(repo_root: Path, args: list[str]) -> str:
    result = subprocess.run(
        ["git", *args],
        cwd=repo_root,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        raise SystemExit(
            "git command failed: git "
            + " ".join(args)
            + ("\n" + result.stderr.strip() if result.stderr.strip() else "")
        )
    return result.stdout.strip()


def git_tags(repo_root: Path) -> list[str]:
    output = run_git(repo_root, ["tag", "--list", "--sort=-v:refname"])
    return [line.strip() for line in output.splitlines() if line.strip()]


def latest_release_tag(repo_root: Path, requested_version: str | None) -> str:
    tags = git_tags(repo_root)
    if requested_version:
        if requested_version not in tags:
            raise SystemExit(f"Requested release tag does not exist in git: {requested_version}")
        return requested_version
    if not tags:
        raise SystemExit("No git tags found. Create a release tag before bundling firmware.")
    return tags[0]


def previous_same_convention_tag(repo_root: Path, release_tag: str) -> str | None:
    match = re.match(r"^(.*?)(\d+)$", release_tag)
    if match is None:
        return None

    prefix = match.group(1)
    release_number = int(match.group(2))
    candidates: list[tuple[int, str]] = []
    for tag in git_tags(repo_root):
        candidate_match = re.match(r"^(.*?)(\d+)$", tag)
        if candidate_match is None or candidate_match.group(1) != prefix:
            continue
        candidate_number = int(candidate_match.group(2))
        if candidate_number < release_number:
            candidates.append((candidate_number, tag))

    if not candidates:
        return None
    return max(candidates, key=lambda item: item[0])[1]


def git_commit_range(previous_tag: str | None, release_tag: str) -> str:
    if previous_tag:
        return f"{previous_tag}..{release_tag}"
    return release_tag


def changed_files_for_commit(repo_root: Path, commit: str) -> tuple[str, ...]:
    output = run_git(
        repo_root,
        ["diff-tree", "--no-commit-id", "--name-only", "-r", commit],
    )
    return tuple(line.strip() for line in output.splitlines() if line.strip())


def git_changes(repo_root: Path, previous_tag: str | None, release_tag: str) -> list[GitChange]:
    output = run_git(
        repo_root,
        [
            "log",
            "--no-merges",
            "--date=short",
            "--pretty=format:%H%x1f%s%x1f%an%x1f%ad",
            git_commit_range(previous_tag, release_tag),
        ],
    )
    changes: list[GitChange] = []
    for line in output.splitlines():
        if not line.strip():
            continue
        parts = line.split("\x1f")
        if len(parts) != 4:
            continue
        commit, subject, author, date = parts
        changes.append(
            GitChange(
                commit=commit,
                subject=subject,
                author=author,
                date=date,
                files=changed_files_for_commit(repo_root, commit),
            )
        )
    return changes


def sentence_from_subject(subject: str) -> str:
    cleaned = re.sub(r"^[a-zA-Z]+(?:\([^)]+\))?:\s*", "", subject).strip()
    if not cleaned:
        cleaned = subject.strip()
    cleaned = cleaned[:1].upper() + cleaned[1:]
    if cleaned[-1:] not in {".", "!", "?"}:
        cleaned += "."
    return cleaned


def categorize_change(change: GitChange) -> str:
    subject = change.subject.lower()
    non_doc_files = [
        path for path in change.files
        if not path.lower().endswith(".md") and not path.startswith("docs/")
    ]

    if any(token in subject for token in ["documentation", "docs", "readme"]):
        return "Documentation"
    if any(token in subject for token in ["sensor", "upload", "backend", "measurement", "bme", "gps", "sps", "sds", "sds011", "dht", "ina", "mhz", "no2", "uart", "transport"]):
        return "Sensors and Uploads"
    if any(token in subject for token in ["config", "nvs", "schema", "migration", "sdkconfig", "kconfig", "partition", "psram"]):
        return "Configuration and Release Behavior"
    if any(token in subject for token in ["network", "wifi", "wi-fi", "cellular", "sntp", "connectivity", "status", "watchdog", "led", "rgb"]):
        return "Connectivity and Runtime"
    if any(token in subject for token in ["web", "ui", "diagnostic", "diagnostics", "page"]):
        return "Web UI"

    if change.files and not non_doc_files:
        return "Documentation"

    files_text = " ".join(non_doc_files).lower()
    text = f"{subject} {files_text}"

    if any(token in text for token in ["sensor", "upload", "backend", "measurement", "bme", "gps", "sps", "sds", "sds011", "dht", "ina", "mhz", "no2", "uart", "transport"]):
        return "Sensors and Uploads"
    if any(token in text for token in ["config", "nvs", "schema", "migration", "sdkconfig", "kconfig", "partition", "psram"]):
        return "Configuration and Release Behavior"
    if any(token in text for token in ["network", "wifi", "wi-fi", "cellular", "sntp", "connectivity", "status_service", "watchdog", "led", "rgb"]):
        return "Connectivity and Runtime"
    if any(token in text for token in ["webui", "web_server", "web_", "/web", "diagnostics", "diagnostic page", "config page", "ui"]):
        return "Web UI"
    if any(token in text for token in ["release", "bundle", "cmake", "build", ".agents/skills", "skill"]):
        return "Build and Tooling"
    return "Maintenance"


def grouped_changes(changes: list[GitChange]) -> dict[str, list[str]]:
    groups: dict[str, list[str]] = {
        "Connectivity and Runtime": [],
        "Web UI": [],
        "Sensors and Uploads": [],
        "Configuration and Release Behavior": [],
        "Documentation": [],
        "Build and Tooling": [],
        "Maintenance": [],
    }
    seen_by_group: dict[str, set[str]] = {name: set() for name in groups}
    for change in reversed(changes):
        category = categorize_change(change)
        item = sentence_from_subject(change.subject)
        if item in seen_by_group[category]:
            continue
        groups[category].append(item)
        seen_by_group[category].add(item)
    return groups


def highlight_items(groups: dict[str, list[str]]) -> list[str]:
    highlights: list[str] = []
    for category in [
        "Connectivity and Runtime",
        "Web UI",
        "Sensors and Uploads",
        "Configuration and Release Behavior",
        "Build and Tooling",
    ]:
        highlights.extend(groups.get(category, [])[:2])
        if len(highlights) >= 6:
            break
    if not highlights:
        highlights.extend(groups.get("Documentation", [])[:4])
    return highlights[:6]


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


def write_release_notes(
    path: Path,
    release_tag: str,
    previous_tag: str | None,
    build_project_version: str,
    bundle_prefix: str,
    target: str,
    flash_size: str,
    full_zip: Path,
    split_zip: Path,
    changes: list[GitChange],
) -> None:
    groups = grouped_changes(changes)
    highlights = highlight_items(groups)
    range_label = f"{previous_tag}...{release_tag}" if previous_tag else release_tag
    intro = (
        "This release is generated from the git changes in "
        f"`{range_label}` and packages the current local firmware build for {target.upper()}."
    )

    lines = [
        "# Release Notes — Air360 Firmware",
        "",
        f"Version: `{release_tag}`",
        f"Changes: `{range_label}`",
        f"Build project version: `{build_project_version}`",
        "",
        intro,
        "",
    ]

    if highlights:
        lines.extend(["## Highlights", ""])
        lines.extend(f"- {item}" for item in highlights)
        lines.append("")

    for section in [
        "Connectivity and Runtime",
        "Web UI",
        "Sensors and Uploads",
        "Configuration and Release Behavior",
        "Documentation",
        "Build and Tooling",
        "Maintenance",
    ]:
        items = groups.get(section, [])
        if not items:
            continue
        lines.extend([f"## {section}", ""])
        lines.extend(f"- {item}" for item in items)
        lines.append("")

    if changes:
        lines.extend(["## Commit Range", ""])
        for change in changes:
            lines.append(f"- `{change.commit[:8]}` {change.date} — {change.subject}")
        lines.append("")

    lines.extend([
        "## Artifacts",
        "",
        f"- Recommended full image: `{full_zip.name}`",
        f"- Manual split-image bundle: `{split_zip.name}`",
        f"- Bundle prefix: `{bundle_prefix}`",
        f"- Hardware target: `{target.upper()}`",
        f"- Flash size: `{flash_size}`",
        "",
    ])

    if not previous_tag:
        lines.extend([
            "## Notes",
            "",
            "- No previous tag with the same trailing-number naming convention was found; release notes include the selected tag history only.",
            "",
        ])

    notes = "\n".join(lines)
    path.write_text(notes, encoding="utf-8")


def main() -> int:
    args = parse_args()

    repo_root = repo_root_from_script()
    release_tag = latest_release_tag(repo_root, args.requested_version)
    previous_tag = previous_same_convention_tag(repo_root, release_tag)
    changes = git_changes(repo_root, previous_tag, release_tag)

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

    bundle_prefix = normalize_prefix(release_tag)
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

    write_release_notes(
        bundle_dir / "release-notes.md",
        release_tag=release_tag,
        previous_tag=previous_tag,
        build_project_version=project_version,
        bundle_prefix=bundle_prefix,
        target=target,
        flash_size=flash_size,
        full_zip=full_zip,
        split_zip=split_zip,
        changes=changes,
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
    print(f"Release tag: {release_tag}")
    print(f"Previous comparable tag: {previous_tag or 'not found'}")
    print(f"Merged image: {merged_bin}")
    print(f"Full zip: {full_zip}")
    print(f"Split zip: {split_zip}")
    print(f"Release notes: {bundle_dir / 'release-notes.md'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
