#!/usr/bin/env python3
"""Check that firmware, backend, and portal agree on sensor types and measurement kinds.

The three code bases each keep their own copy of the wire-level names:

- firmware: `sensorTypeKey()` and `sensorValueKindKey()` in
  `firmware/main/include/air360/sensors/sensor_types.hpp`
- backend:  `sensorTypes` / `measurementKinds` / `derivedMeasurementKinds` in
  `backend/src/contracts/*.ts`
- portal:   `sensorTypes` and the `kindLabel()` table in `portal/src/lib/api.ts`

Run from the repository root:

    python3 scripts/check_api_contracts.py

Exit status is non-zero when any list drifts.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FIRMWARE_TYPES = ROOT / "firmware/main/include/air360/sensors/sensor_types.hpp"
BACKEND_SENSOR_TYPES = ROOT / "backend/src/contracts/sensor-type.ts"
BACKEND_KINDS = ROOT / "backend/src/contracts/measurement-kind.ts"
PORTAL_API = ROOT / "portal/src/lib/api.ts"

# Firmware-only enum values that never reach the wire.
FIRMWARE_IGNORED = {"unknown", "none"}


def ts_const_array(source: str, name: str) -> list[str]:
    match = re.search(rf"export const {name} = \[(.*?)\] as const;", source, re.S)
    if not match:
        raise SystemExit(f"could not find `{name}` array")
    return re.findall(r'"([a-z0-9_]+)"', match.group(1))


def cpp_key_function(source: str, name: str) -> list[str]:
    match = re.search(
        rf"inline const char\* {name}\([^)]*\) \{{(.*?)\n\}}", source, re.S
    )
    if not match:
        raise SystemExit(f"could not find `{name}()` in {FIRMWARE_TYPES}")
    values = re.findall(r'return "([a-z0-9_]+)";', match.group(1))
    return [value for value in values if value not in FIRMWARE_IGNORED]


def portal_kind_labels(source: str) -> list[str]:
    match = re.search(
        r"export function kindLabel\(kind: string\): string \{\s*const labels: Record<string, string> = \{(.*?)\};",
        source,
        re.S,
    )
    if not match:
        raise SystemExit("could not find the kindLabel() table in portal api.ts")
    return re.findall(r"^\s+([a-z0-9_]+):", match.group(1), re.M)


def compare(label: str, expected: list[str], actual: list[str], ordered: bool = False) -> list[str]:
    problems: list[str] = []
    missing = sorted(set(expected) - set(actual))
    extra = sorted(set(actual) - set(expected))
    if missing:
        problems.append(f"{label}: missing {', '.join(missing)}")
    if extra:
        problems.append(f"{label}: unexpected {', '.join(extra)}")
    if ordered and not problems and expected != actual:
        problems.append(f"{label}: same members but different order")
    duplicates = sorted({v for v in actual if actual.count(v) > 1})
    if duplicates:
        problems.append(f"{label}: duplicated {', '.join(duplicates)}")
    return problems


def main() -> int:
    firmware = FIRMWARE_TYPES.read_text(encoding="utf-8")
    backend_types = BACKEND_SENSOR_TYPES.read_text(encoding="utf-8")
    backend_kinds = BACKEND_KINDS.read_text(encoding="utf-8")
    portal = PORTAL_API.read_text(encoding="utf-8")

    fw_sensor_types = cpp_key_function(firmware, "sensorTypeKey")
    fw_kinds = cpp_key_function(firmware, "sensorValueKindKey")
    be_sensor_types = ts_const_array(backend_types, "sensorTypes")
    be_kinds = ts_const_array(backend_kinds, "measurementKinds")
    be_derived = ts_const_array(backend_kinds, "derivedMeasurementKinds")
    portal_sensor_types = ts_const_array(portal, "sensorTypes")
    portal_kinds = portal_kind_labels(portal)

    problems: list[str] = []
    problems += compare("backend sensorTypes vs firmware sensorTypeKey", fw_sensor_types, be_sensor_types)
    problems += compare("backend measurementKinds vs firmware sensorValueKindKey", fw_kinds, be_kinds)
    problems += compare("portal sensorTypes vs backend sensorTypes", be_sensor_types, portal_sensor_types)
    problems += compare(
        "portal kindLabel table vs backend measurementKinds + derived",
        be_kinds + be_derived,
        portal_kinds,
    )

    if problems:
        print("API contract check failed:")
        for problem in problems:
            print(f"  - {problem}")
        return 1

    print(
        f"API contracts in sync: {len(be_sensor_types)} sensor types, "
        f"{len(be_kinds)} measurement kinds, {len(be_derived)} derived."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
