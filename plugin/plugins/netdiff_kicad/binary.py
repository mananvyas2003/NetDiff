#!/usr/bin/env python3
"""Locate the netdiff CLI binary (plugin never reimplements connectivity)."""

from __future__ import annotations

import os
import shutil
import sys
from pathlib import Path


def candidate_paths(plugin_root: Path | None = None) -> list[Path]:
    paths: list[Path] = []
    env = os.environ.get("NETDIFF_BIN")
    if env:
        paths.append(Path(env))

    which = shutil.which("netdiff")
    if which:
        paths.append(Path(which))

    # Bundled next to the plugin (optional PCM layout: plugins/bin/netdiff).
    here = plugin_root or Path(__file__).resolve().parent
    for rel in (
        Path("bin") / "netdiff",
        Path("bin") / "netdiff.exe",
        Path("..") / "bin" / "netdiff",
        Path("..") / "bin" / "netdiff.exe",
    ):
        paths.append((here / rel).resolve())

    # Common local build outputs when developing NetDiff itself.
    repo = here
    for _ in range(6):
        repo = repo.parent
        for rel in (
            Path("build") / "cli" / "netdiff",
            Path("build") / "cli" / "netdiff.exe",
            Path("build") / "cli" / "Release" / "netdiff.exe",
        ):
            paths.append(repo / rel)

    # Deduplicate while preserving order.
    seen: set[str] = set()
    out: list[Path] = []
    for path in paths:
        key = str(path)
        if key in seen:
            continue
        seen.add(key)
        out.append(path)
    return out


def find_netdiff_binary(plugin_root: Path | None = None) -> Path | None:
    for path in candidate_paths(plugin_root):
        if path.is_file():
            return path
    return None


def require_netdiff_binary(plugin_root: Path | None = None) -> Path:
    found = find_netdiff_binary(plugin_root)
    if found is None:
        raise FileNotFoundError(
            "netdiff binary not found. Install a release, put it on PATH, "
            "set NETDIFF_BIN, or place it under plugin/bin/."
        )
    return found


if __name__ == "__main__":
    try:
        print(require_netdiff_binary())
    except FileNotFoundError as exc:
        print(exc, file=sys.stderr)
        raise SystemExit(2) from exc
