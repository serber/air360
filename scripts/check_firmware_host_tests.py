#!/usr/bin/env python3

from __future__ import annotations

import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent
HOST_TEST_DIR = REPO_ROOT / "firmware" / "test" / "host"
HOST_TEST_BUILD_DIR = HOST_TEST_DIR / "build"


def run(command: list[str]) -> None:
    print("+ " + " ".join(command))
    subprocess.run(command, cwd=REPO_ROOT, check=True)


def main() -> int:
    try:
        run(["cmake", "-S", str(HOST_TEST_DIR), "-B", str(HOST_TEST_BUILD_DIR)])
        run(["cmake", "--build", str(HOST_TEST_BUILD_DIR)])
        run(["ctest", "--test-dir", str(HOST_TEST_BUILD_DIR), "--output-on-failure"])
    except subprocess.CalledProcessError as error:
        return error.returncode
    return 0


if __name__ == "__main__":
    sys.exit(main())
