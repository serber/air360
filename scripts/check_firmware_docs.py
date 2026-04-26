#!/usr/bin/env python3

from __future__ import annotations

import re
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent
LINK_RE = re.compile(r"\[[^]]+\]\(([^)]+)\)")
EXTERNAL_PREFIXES = ("http://", "https://", "mailto:", "tel:")
REQUIRED_HEADERS = (
    "## Status",
    "## Scope",
    "## Source of truth in code",
    "## Read next",
)


def iter_markdown_files() -> list[Path]:
    files = [
        REPO_ROOT / "AGENTS.md",
        REPO_ROOT / "CLAUDE.md",
        REPO_ROOT / "firmware" / "AGENTS.md",
        REPO_ROOT / "firmware" / "CLAUDE.md",
        REPO_ROOT / "firmware" / "README.md",
    ]
    files.extend(sorted((REPO_ROOT / "docs" / "firmware").rglob("*.md")))
    return files


def check_links(files: list[Path]) -> list[str]:
    errors: list[str] = []
    for path in files:
        text = path.read_text(encoding="utf-8")
        for line_no, line in enumerate(text.splitlines(), start=1):
            for raw_target in LINK_RE.findall(line):
                if raw_target.startswith(EXTERNAL_PREFIXES) or raw_target.startswith("#"):
                    continue
                if raw_target.startswith("@"):
                    continue
                target = raw_target.split("#", 1)[0]
                resolved = (path.parent / target).resolve()
                if not resolved.exists():
                    rel = path.relative_to(REPO_ROOT)
                    errors.append(f"Broken link: {rel}:{line_no} -> {target}")
    return errors


def check_headers() -> list[str]:
    errors: list[str] = []
    for path in sorted((REPO_ROOT / "docs" / "firmware").rglob("*.md")):
        rel = path.relative_to(REPO_ROOT)
        if rel.parts[2] == "adr":
            continue
        if rel == Path("docs/firmware/doc-template.md"):
            continue
        text = path.read_text(encoding="utf-8")
        for header in REQUIRED_HEADERS:
            if header not in text:
                errors.append(f"Missing header '{header}' in {rel}")
    return errors


def check_sensor_index() -> list[str]:
    errors: list[str] = []
    sensor_dir = REPO_ROOT / "docs" / "firmware" / "sensors"
    index_text = (sensor_dir / "README.md").read_text(encoding="utf-8")
    for path in sorted(sensor_dir.glob("*.md")):
        if path.name in {"README.md", "adding-new-sensor.md", "supported-sensors.md"}:
            continue
        if path.name not in index_text:
            rel = path.relative_to(REPO_ROOT)
            errors.append(f"Sensor doc missing from sensors/README.md: {rel}")
    return errors


def check_adr_index() -> list[str]:
    errors: list[str] = []
    adr_dir = REPO_ROOT / "docs" / "firmware" / "adr"
    index_text = (adr_dir / "README.md").read_text(encoding="utf-8")
    for path in sorted(adr_dir.glob("*.md")):
        if path.name == "README.md":
            continue
        if path.name not in index_text:
            rel = path.relative_to(REPO_ROOT)
            errors.append(f"ADR missing from adr/README.md: {rel}")
    return errors


def main() -> int:
    files = iter_markdown_files()
    errors: list[str] = []
    errors.extend(check_links(files))
    errors.extend(check_headers())
    errors.extend(check_sensor_index())
    errors.extend(check_adr_index())

    if errors:
        for error in errors:
            print(error)
        return 1

    print("Firmware docs check passed.")
    print(f"Checked {len(files)} markdown files.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
