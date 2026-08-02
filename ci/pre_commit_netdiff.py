#!/usr/bin/env python3
"""pre-commit entry: `netdiff diff --staged` (04_CLI_CI_SPEC.md §2.3).

Looks for a `netdiff` binary on PATH, or NETDIFF_BIN, or a common local build path.
Exit codes mirror the CLI contract (0 pass, 1 gate fail → block commit).
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path


def find_binary() -> str | None:
    env = os.environ.get("NETDIFF_BIN")
    if env and Path(env).is_file():
        return env
    which = shutil.which("netdiff")
    if which:
        return which
    root = Path(__file__).resolve().parents[1]
    for candidate in (
        root / "build" / "cli" / "netdiff",
        root / "build" / "cli" / "netdiff.exe",
        root / "build" / "cli" / "Release" / "netdiff.exe",
    ):
        if candidate.is_file():
            return str(candidate)
    return None


def main(argv: list[str]) -> int:
    binary = find_binary()
    if not binary:
        print(
            "netdiff: binary not found. Install a release, set NETDIFF_BIN, or build "
            "`cmake --build build --target netdiff_cli`.",
            file=sys.stderr,
        )
        return 2

    path = "."
    if len(argv) > 1:
        path = argv[1]

    cmd = [binary, "diff", "--staged", path, "--no-color"]
    fail_on = os.environ.get("NETDIFF_FAIL_ON")
    if fail_on:
        cmd += ["--fail-on", fail_on]
    config = os.environ.get("NETDIFF_CONFIG")
    if config:
        cmd += ["--config", config]

    proc = subprocess.run(cmd, text=True, encoding="utf-8", errors="replace")
    if proc.returncode == 1:
        print(
            "netdiff: significant connectivity changes in the index "
            "(commit with --no-verify to bypass).",
            file=sys.stderr,
        )
    return proc.returncode


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
