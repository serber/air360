#!/usr/bin/env python3
"""Cut an Air360 firmware release end to end: verify the tree, tag, build, bundle.

Run from anywhere inside the repository:

    python3 .claude/skills/air360-firmware-release-bundle/scripts/release_firmware.py v1.4

Steps, each of which stops the run on failure:

1. Preflight: valid version string, on the release branch (``main`` by
   default), clean tracked tree, not behind ``origin/<branch>``.
2. Tag: create the annotated tag on HEAD (or reuse it with ``--reuse-tag`` when
   it already points at HEAD).
3. Build: ``idf.py fullclean`` + ``idf.py build`` through the ESP-IDF export
   script, so ``git describe`` embeds the tag in the app descriptor and in
   ``build/project_description.json``.
4. Bundle: package ``firmware/build`` into ``firmware/release/air360-<tag>/``
   via ``create_release_bundle.py``.
5. Optionally push the branch and the tag (``--push``).

A tag created by this run is deleted again if any later step fails, so a
fixed-up rerun starts from a clean state.
"""
from __future__ import annotations

import argparse
import os
import re
import shlex
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from create_release_bundle import (  # noqa: E402
    create_bundle,
    normalize_version,
    print_bundle_summary,
    repo_root_from_script,
)

DEFAULT_BRANCH = "main"
DEFAULT_IDF_EXPORT = Path.home() / ".espressif" / "v6.0" / "esp-idf" / "export.sh"
VERSION_PATTERN = re.compile(r"^v\d+\.\d+(?:\.\d+)?(?:-(?:alpha|beta|rc)\.\d+)?$")


class ReleaseError(SystemExit):
    """A failed release step; the message is printed and the run exits non-zero."""

    def __init__(self, message: str) -> None:
        super().__init__(f"error: {message}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Tag, build, and bundle an Air360 firmware release from the current HEAD.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "examples:\n"
            "  release_firmware.py v1.4                 stable release from main\n"
            "  release_firmware.py v1.5-beta.1 --push   beta, then push branch and tag\n"
            "  release_firmware.py v1.4 --reuse-tag     re-bundle an already tagged HEAD\n"
        ),
    )
    parser.add_argument("version", help="Release tag to create, e.g. v1.4 or v1.5-beta.1 (a leading v is added if missing).")
    parser.add_argument("--branch", default=DEFAULT_BRANCH, help=f"Branch releases are cut from (default: {DEFAULT_BRANCH}).")
    parser.add_argument("--any-branch", action="store_true", help="Skip the branch check and release from the current HEAD.")
    parser.add_argument("--message", "-m", default=None, help="Annotated tag message (default: 'Air360 firmware <version>').")
    parser.add_argument("--reuse-tag", action="store_true", help="Accept an existing tag if it already points at HEAD instead of failing.")
    parser.add_argument("--no-fetch", action="store_true", help="Do not fetch origin before checking whether the branch is behind.")
    parser.add_argument("--no-clean", action="store_true", help="Run 'idf.py reconfigure build' instead of a full clean rebuild.")
    parser.add_argument("--skip-build", action="store_true", help="Package the existing firmware/build without rebuilding (must already carry the tag).")
    parser.add_argument("--push", action="store_true", help="Push the branch and the tag to origin after the bundle is created.")
    parser.add_argument(
        "--idf-export",
        type=Path,
        default=Path(os.environ.get("AIR360_IDF_EXPORT", str(DEFAULT_IDF_EXPORT))),
        help=f"ESP-IDF export.sh to source before idf.py (default: {DEFAULT_IDF_EXPORT}).",
    )
    return parser.parse_args()


def step(title: str) -> None:
    print(f"\n==> {title}", flush=True)


def git(repo_root: Path, *args: str, check: bool = True, capture: bool = True) -> str:
    result = subprocess.run(
        ["git", "-C", str(repo_root), *args],
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.PIPE if capture else None,
        text=True,
        check=False,
    )
    if check and result.returncode != 0:
        detail = (result.stderr or result.stdout or "").strip() if capture else ""
        raise ReleaseError(f"git {' '.join(args)} failed" + (f": {detail}" if detail else ""))
    return (result.stdout or "").strip() if capture else ""


def git_ok(repo_root: Path, *args: str) -> bool:
    return subprocess.run(
        ["git", "-C", str(repo_root), *args],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    ).returncode == 0


def preflight(repo_root: Path, version: str, args: argparse.Namespace) -> str:
    """Validate the version and the working tree; return the current branch name."""
    if not VERSION_PATTERN.match(version):
        raise ReleaseError(
            f"'{version}' is not a release version. Expected vMAJOR.MINOR[.PATCH][-alpha.N|-beta.N|-rc.N], e.g. v1.4 or v1.5-beta.1."
        )

    branch = git(repo_root, "rev-parse", "--abbrev-ref", "HEAD")
    if branch == "HEAD":
        raise ReleaseError("HEAD is detached. Check out the release branch first.")
    if not args.any_branch and branch != args.branch:
        raise ReleaseError(
            f"on branch '{branch}', releases are cut from '{args.branch}'. "
            f"Check out {args.branch} (merge the release PR first) or pass --any-branch."
        )

    dirty = git(repo_root, "status", "--porcelain", "--untracked-files=no")
    if dirty:
        raise ReleaseError(
            "the working tree has uncommitted changes; ESP-IDF would embed a -dirty version:\n" + dirty
        )

    if not args.no_fetch and git_ok(repo_root, "remote", "get-url", "origin"):
        try:
            fetch = subprocess.run(
                ["git", "-C", str(repo_root), "fetch", "--quiet", "origin", "--tags"],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                check=False,
                timeout=120,
            )
            fetch_error = fetch.stderr.strip() if fetch.returncode != 0 else ""
        except subprocess.TimeoutExpired:
            fetch_error = "timed out after 120 s"
        if fetch_error:
            print(f"warning: git fetch origin failed, continuing with local refs: {fetch_error}")

    upstream = f"origin/{branch}"
    if git_ok(repo_root, "rev-parse", "--verify", "--quiet", upstream):
        behind = int(git(repo_root, "rev-list", "--count", f"HEAD..{upstream}") or "0")
        ahead = int(git(repo_root, "rev-list", "--count", f"{upstream}..HEAD") or "0")
        if behind:
            raise ReleaseError(f"{branch} is {behind} commit(s) behind {upstream}. Pull first so the tag lands on the published history.")
        if ahead:
            print(f"note: {branch} is {ahead} commit(s) ahead of {upstream}; use --push (or push by hand) after the bundle is created.")
    else:
        print(f"note: no {upstream} ref; skipping the behind/ahead check.")

    return branch


def ensure_tag(repo_root: Path, version: str, message: str, reuse: bool) -> bool:
    """Create the release tag on HEAD. Returns True if this run created it."""
    head = git(repo_root, "rev-parse", "HEAD")
    if git_ok(repo_root, "rev-parse", "--verify", "--quiet", f"refs/tags/{version}"):
        tag_commit = git(repo_root, "rev-parse", f"{version}^{{commit}}")
        if tag_commit != head:
            raise ReleaseError(
                f"tag {version} already exists on {tag_commit[:7]}, but HEAD is {head[:7]}. "
                "Pick the next version, or check out the tagged commit and pass --reuse-tag."
            )
        if not reuse:
            raise ReleaseError(f"tag {version} already exists on HEAD. Pass --reuse-tag to rebuild and re-bundle it.")
        print(f"Reusing existing tag {version} on {head[:7]}.")
        return False

    git(repo_root, "tag", "-a", version, "-m", message)
    print(f"Created tag {version} on {head[:7]}.")
    return True


def run_idf(firmware_dir: Path, idf_export: Path, idf_args: list[str]) -> None:
    if not idf_export.is_file():
        raise ReleaseError(f"ESP-IDF export script not found: {idf_export} (override with --idf-export or AIR360_IDF_EXPORT).")
    command = f"source {shlex.quote(str(idf_export))} >/dev/null && idf.py {' '.join(shlex.quote(a) for a in idf_args)}"
    print(f"$ (cd {firmware_dir} && {command})", flush=True)
    result = subprocess.run(["bash", "-c", command], cwd=str(firmware_dir), check=False)
    if result.returncode != 0:
        raise ReleaseError(f"idf.py {' '.join(idf_args)} failed with exit code {result.returncode}")


def build_firmware(firmware_dir: Path, idf_export: Path, full_clean: bool) -> None:
    # ESP-IDF evaluates `git describe` only when CMake configures, and a new tag
    # does not invalidate the CMake cache on its own. Reconfiguring is therefore
    # mandatory; a full clean additionally guarantees no stale objects ship.
    if full_clean:
        run_idf(firmware_dir, idf_export, ["fullclean"])
        run_idf(firmware_dir, idf_export, ["build"])
    else:
        run_idf(firmware_dir, idf_export, ["reconfigure", "build"])


def verify_build_version(firmware_dir: Path, version: str) -> None:
    import json

    description = firmware_dir / "build" / "project_description.json"
    if not description.is_file():
        raise ReleaseError(f"{description} not found; the build did not complete.")
    built = normalize_version(json.loads(description.read_text(encoding="utf-8"))["project_version"])
    if built != version:
        raise ReleaseError(
            f"build embeds project_version '{built}', expected '{version}'. "
            "The build did not pick up the tag; rerun without --skip-build/--no-clean."
        )
    print(f"Build project_version: {built}")


def push(repo_root: Path, branch: str, version: str) -> None:
    git(repo_root, "push", "origin", branch, capture=False)
    git(repo_root, "push", "origin", f"refs/tags/{version}", capture=False)


def github_release_url(repo_root: Path, version: str) -> str | None:
    url = git(repo_root, "remote", "get-url", "origin", check=False)
    match = re.match(r"^(?:https://github\.com/|git@github\.com:)([^/]+/[^/]+?)(?:\.git)?/?$", url)
    if not match:
        return None
    return f"https://github.com/{match.group(1)}/releases/new?tag={version}"


def main() -> int:
    args = parse_args()
    version = normalize_version(args.version)
    repo_root = repo_root_from_script()
    firmware_dir = repo_root / "firmware"
    release_root = firmware_dir / "release"
    message = args.message or f"Air360 firmware {version}"

    step(f"Preflight for {version}")
    branch = preflight(repo_root, version, args)
    print(f"Branch {branch} at {git(repo_root, 'rev-parse', '--short', 'HEAD')}, tree clean.")

    step("Tag")
    created_tag = ensure_tag(repo_root, version, message, reuse=args.reuse_tag)

    try:
        if args.skip_build:
            step("Build (skipped)")
        else:
            step("Build" if args.no_clean else "Build (full clean)")
            build_firmware(firmware_dir, args.idf_export, full_clean=not args.no_clean)
        verify_build_version(firmware_dir, version)

        step("Bundle")
        result = create_bundle(
            version,
            repo_root=repo_root,
            firmware_dir=firmware_dir,
            release_root=release_root,
        )
        print_bundle_summary(result)
    except BaseException:
        if created_tag:
            git(repo_root, "tag", "-d", version, check=False)
            print(f"Rolled back tag {version} (created by this run).", file=sys.stderr)
        raise

    if args.push:
        step("Push")
        push(repo_root, branch, version)
        print(f"Pushed {branch} and tag {version} to origin.")

    step("Done")
    print(f"Bundle: {result.bundle_dir}")
    print("Next:")
    print(f"  1. Review and edit {result.release_notes.name} (testing notes, sign-off).")
    if not args.push:
        print(f"  2. git push origin {branch} && git push origin {version}")
    release_url = github_release_url(repo_root, version)
    upload_list = ", ".join(
        p.name for p in (result.merged_bin, result.ota_bin, result.split_zip, result.checksums, result.release_notes)
    )
    step_no = 2 if args.push else 3
    if release_url:
        print(f"  {step_no}. Create the GitHub release at {release_url} and upload: {upload_list}")
    else:
        print(f"  {step_no}. Create the GitHub release for {version} and upload: {upload_list}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit("interrupted")
