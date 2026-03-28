#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  validate.sh [firmware-project-root]

Examples:
  validate.sh
  validate.sh /path/to/firmware

Behavior:
  - If a project path is provided, validate that ESP-IDF project.
  - Otherwise, search upward from the current directory for a folder containing
    CMakeLists.txt and main/CMakeLists.txt, and use that as the ESP-IDF project root.
EOF
}

find_project_root() {
  local dir="$1"
  while [[ "$dir" != "/" ]]; do
    if [[ -f "$dir/CMakeLists.txt" && -f "$dir/main/CMakeLists.txt" ]]; then
      printf '%s\n' "$dir"
      return 0
    fi
    dir="$(dirname "$dir")"
  done
  return 1
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

if ! command -v idf.py >/dev/null 2>&1; then
  echo "error: idf.py is not on PATH. Load the ESP-IDF environment first." >&2
  echo "hint: . \"\$HOME/.espressif/<version>/esp-idf/export.sh\"" >&2
  exit 1
fi

if [[ -z "${IDF_PATH:-}" ]]; then
  echo "error: IDF_PATH is not set. Load the ESP-IDF environment first." >&2
  echo "hint: . \"\$HOME/.espressif/<version>/esp-idf/export.sh\"" >&2
  exit 1
fi

project_root=""
if [[ $# -ge 1 ]]; then
  project_root="${1:A}"
  if [[ ! -d "$project_root" ]]; then
    echo "error: project root does not exist: $project_root" >&2
    exit 1
  fi
else
  if ! project_root="$(find_project_root "${PWD:A}")"; then
    echo "error: could not find an ESP-IDF project root from current directory." >&2
    echo "hint: run this script from inside a firmware project or pass the project path explicitly." >&2
    exit 1
  fi
fi

if [[ ! -f "$project_root/CMakeLists.txt" || ! -f "$project_root/main/CMakeLists.txt" ]]; then
  echo "error: not an ESP-IDF project root: $project_root" >&2
  exit 1
fi

echo "==> validating ESP-IDF project: $project_root"
cd "$project_root"

idf.py build
idf.py size
