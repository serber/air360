#!/usr/bin/env bash
# Air360 firmware validation: ESP-IDF build + repository checkers + host tests.
#
# Usage:
#   validate.sh [--build-only | --no-build] [firmware-project-root]
#
# Runs, in order:
#   1. idf.py build   (after sourcing the ESP-IDF environment; idf.py is not on PATH)
#   2. scripts/check_style.py
#   3. scripts/check_firmware_docs.py
#   4. scripts/check_firmware_host_tests.py   (CMake/CTest host tests)
#
# The full build log goes to $TMPDIR (path printed); only errors/warnings from the
# project sources are echoed, managed_components noise is filtered out.
set -euo pipefail

ESP_IDF_EXPORT="${ESP_IDF_EXPORT:-$HOME/.espressif/v6.0/esp-idf/export.sh}"

usage() {
  sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'
}

find_project_root() {
  local dir="$1"
  while [[ "$dir" != "/" ]]; do
    if [[ -f "$dir/CMakeLists.txt" && -f "$dir/main/CMakeLists.txt" ]]; then
      printf '%s\n' "$dir"
      return 0
    fi
    if [[ -f "$dir/firmware/CMakeLists.txt" && -f "$dir/firmware/main/CMakeLists.txt" ]]; then
      printf '%s\n' "$dir/firmware"
      return 0
    fi
    dir="$(dirname "$dir")"
  done
  return 1
}

run_build=1
run_checks=1
project_root=""
for arg in "$@"; do
  case "$arg" in
    -h|--help) usage; exit 0 ;;
    --build-only) run_checks=0 ;;
    --no-build) run_build=0 ;;
    *) project_root="$(cd "$arg" 2>/dev/null && pwd)" || { echo "error: no such directory: $arg" >&2; exit 1; } ;;
  esac
done

if [[ -z "$project_root" ]]; then
  if ! project_root="$(find_project_root "$PWD")"; then
    echo "error: could not find an ESP-IDF project root (CMakeLists.txt + main/CMakeLists.txt) from $PWD" >&2
    exit 1
  fi
fi
if [[ ! -f "$project_root/CMakeLists.txt" || ! -f "$project_root/main/CMakeLists.txt" ]]; then
  echo "error: not an ESP-IDF project root: $project_root" >&2
  exit 1
fi
repo_root="$(cd "$project_root/.." && pwd)"

status=0

if [[ "$run_build" -eq 1 ]]; then
  if ! command -v idf.py >/dev/null 2>&1; then
    if [[ ! -f "$ESP_IDF_EXPORT" ]]; then
      echo "error: idf.py not on PATH and $ESP_IDF_EXPORT not found (set ESP_IDF_EXPORT)" >&2
      exit 1
    fi
    # export.sh is chatty and not set -u clean.
    set +u
    # shellcheck disable=SC1090
    source "$ESP_IDF_EXPORT" >/dev/null 2>&1
    set -u
  fi

  log_file="${TMPDIR:-/tmp}/air360-idf-build.log"
  echo "==> idf.py build in $project_root (log: $log_file)"
  if (cd "$project_root" && idf.py build >"$log_file" 2>&1); then
    echo "    build OK"
  else
    echo "    build FAILED"
    status=1
  fi
  if grep -n "error\|warning:" "$log_file" | grep -v managed_components | grep -q .; then
    echo "--- errors/warnings from project sources:"
    grep -n "error\|warning:" "$log_file" | grep -v managed_components | head -40
  fi
  [[ "$status" -ne 0 ]] && exit "$status"
fi

if [[ "$run_checks" -eq 1 ]]; then
  for checker in check_style.py check_firmware_docs.py check_firmware_host_tests.py; do
    script="$repo_root/scripts/$checker"
    if [[ ! -f "$script" ]]; then
      echo "==> skip $checker (not found)"
      continue
    fi
    echo "==> $checker"
    if (cd "$repo_root" && python3 "$script"); then
      echo "    OK"
    else
      echo "    FAILED"
      status=1
    fi
  done
fi

exit "$status"
