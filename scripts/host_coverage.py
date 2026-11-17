#!/usr/bin/env python3
"""Line coverage of the firmware host tests, with an explicit denominator.

Builds firmware/test/host with clang source-based coverage, runs CTest, and
reports line coverage only for production files under firmware/main/src that
the host tests actually compile. It also states how many production .cpp files
exist in total, so the number is never read as whole-firmware coverage.

Requires clang/clang++ and llvm-profdata/llvm-cov (found on PATH, as
versioned names such as llvm-cov-18, through xcrun on macOS, or via the
LLVM_PROFDATA / LLVM_COV environment variables).

Writes Markdown to stdout and appends it to $GITHUB_STEP_SUMMARY when set.
"""

from __future__ import annotations

import glob
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
HOST_TEST_DIR = REPO_ROOT / "firmware" / "test" / "host"
BUILD_DIR = HOST_TEST_DIR / "build-coverage"
PRODUCTION_SRC = REPO_ROOT / "firmware" / "main" / "src"
COVERAGE_FLAGS = "-fprofile-instr-generate -fcoverage-mapping"


def find_tool(name: str, env_var: str) -> list[str]:
    if os.environ.get(env_var):
        return [os.environ[env_var]]
    if shutil.which(name):
        return [name]
    versioned = sorted(glob.glob(f"/usr/bin/{name}-[0-9]*"), reverse=True)
    if versioned:
        return [versioned[0]]
    if sys.platform == "darwin" and shutil.which("xcrun"):
        return ["xcrun", name]
    raise SystemExit(f"error: {name} not found (set {env_var})")


def run(command: list[str], **kwargs) -> subprocess.CompletedProcess:
    print("+ " + " ".join(command), file=sys.stderr)
    return subprocess.run(command, check=True, **kwargs)


def main() -> int:
    profdata = find_tool("llvm-profdata", "LLVM_PROFDATA")
    llvm_cov = find_tool("llvm-cov", "LLVM_COV")
    cxx = os.environ.get("CXX", "clang++")

    shutil.rmtree(BUILD_DIR, ignore_errors=True)
    run([
        "cmake", "-S", str(HOST_TEST_DIR), "-B", str(BUILD_DIR),
        f"-DCMAKE_CXX_COMPILER={cxx}",
        f"-DCMAKE_CXX_FLAGS={COVERAGE_FLAGS}",
        f"-DCMAKE_EXE_LINKER_FLAGS={COVERAGE_FLAGS}",
    ], stdout=sys.stderr)
    run(["cmake", "--build", str(BUILD_DIR), "--parallel"], stdout=sys.stderr)

    profile_dir = BUILD_DIR / "profiles"
    profile_dir.mkdir()
    env = dict(os.environ, LLVM_PROFILE_FILE=str(profile_dir / "%p.profraw"))
    run(["ctest", "--test-dir", str(BUILD_DIR), "--output-on-failure"],
        env=env, stdout=sys.stderr)

    merged = BUILD_DIR / "merged.profdata"
    raw_profiles = sorted(str(path) for path in profile_dir.glob("*.profraw"))
    run([*profdata, "merge", "-sparse", *raw_profiles, "-o", str(merged)])

    executables = sorted(
        str(path) for path in BUILD_DIR.glob("test_*")
        if path.is_file() and os.access(path, os.X_OK))
    objects: list[str] = [executables[0]]
    for extra in executables[1:]:
        objects += ["-object", extra]
    exported = run(
        [*llvm_cov, "export", "-summary-only", f"-instr-profile={merged}", *objects],
        capture_output=True, text=True).stdout
    files = json.loads(exported)["data"][0]["files"]

    production_root = str(PRODUCTION_SRC.resolve())
    rows = []
    covered_total = 0
    count_total = 0
    for entry in files:
        path = str(Path(entry["filename"]).resolve())
        if not path.startswith(production_root + os.sep):
            continue
        lines = entry["summary"]["lines"]
        rows.append((os.path.relpath(path, production_root), lines["covered"], lines["count"]))
        covered_total += lines["covered"]
        count_total += lines["count"]
    rows.sort()

    all_production = sorted(PRODUCTION_SRC.rglob("*.cpp"))
    compiled = {name for name, _, _ in rows if name.endswith(".cpp")}

    def pct(covered: int, count: int) -> str:
        return f"{100.0 * covered / count:.1f}%" if count else "n/a"

    out = [
        "## Firmware host-test coverage",
        "",
        f"**Scope:** {len(compiled)} of {len(all_production)} production `.cpp` files under "
        "`firmware/main/src` are compiled into host tests. The percentage below covers "
        "only those files; it is **not** whole-firmware coverage.",
        "",
        f"**Lines covered in scope:** {covered_total} / {count_total} "
        f"({pct(covered_total, count_total)})",
        "",
        "| File | Lines | Coverage |",
        "|------|------:|---------:|",
    ]
    out += [f"| `{name}` | {covered}/{count} | {pct(covered, count)} |"
            for name, covered, count in rows]
    report = "\n".join(out) + "\n"

    print(report)
    summary_path = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary_path:
        with open(summary_path, "a", encoding="utf-8") as summary:
            summary.write(report)
    return 0


if __name__ == "__main__":
    sys.exit(main())
