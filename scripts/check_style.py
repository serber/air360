#!/usr/bin/env python3
"""Style checker for firmware C++ sources.

Currently enforces the log-tag convention:
  - No `static const char* TAG` (or similar) definitions.
  - Every kTag definition must have value matching `"air360.<subsystem>"`.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
FIRMWARE_SRC = REPO_ROOT / "firmware" / "main" / "src"

# Matches legacy ESP-IDF-style tag declarations, e.g.:
#   static const char* TAG = "..."
#   static const char TAG[] = "..."
_LEGACY_TAG_RE = re.compile(
    r'\bstatic\s+const\s+char\s*[\*\[]\s*TAG\b',
)

# Matches constexpr kTag definitions to validate the value format, e.g.:
#   constexpr char kTag[] = "air360.net";
_KTAG_DEFN_RE = re.compile(
    r'\bconstexpr\s+char\s+kTag\b[^=]*=\s*"([^"]*)"',
)

_VALID_TAG_RE = re.compile(r'^air360\.[a-z][a-z0-9_.]*$')


def check_file(path: Path) -> list[str]:
    errors: list[str] = []
    text = path.read_text(encoding="utf-8")
    rel = path.relative_to(REPO_ROOT)

    for lineno, line in enumerate(text.splitlines(), start=1):
        if _LEGACY_TAG_RE.search(line):
            errors.append(f"{rel}:{lineno}: legacy TAG declaration — use `constexpr char kTag[]`")

        for m in _KTAG_DEFN_RE.finditer(line):
            value = m.group(1)
            if not _VALID_TAG_RE.match(value):
                errors.append(
                    f'{rel}:{lineno}: kTag value "{value}" does not match air360.<subsystem> pattern'
                )

    return errors


def main() -> int:
    source_files = sorted(FIRMWARE_SRC.rglob("*.cpp")) + sorted(FIRMWARE_SRC.rglob("*.h"))
    errors: list[str] = []
    for path in source_files:
        errors.extend(check_file(path))

    if errors:
        for error in errors:
            print(error)
        return 1

    print(f"Style check passed ({len(source_files)} files checked).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
