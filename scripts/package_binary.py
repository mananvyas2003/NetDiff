#!/usr/bin/env python3
"""T2.1 — package the built `netdiff` CLI into a release archive + checksum.

Usage:
  python scripts/package_binary.py --binary path/to/netdiff --name netdiff-linux-x86_64 \\
      --version 0.1.0 --out dist

Produces:
  dist/<name>.tar.gz   (Unix)  or  dist/<name>.zip  (Windows name hint / --format)
  dist/<name>.sha256
"""

from __future__ import annotations

import argparse
import hashlib
import os
import platform
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
import zipfile
from pathlib import Path


def detect_default_name() -> str:
    system = platform.system().lower()
    machine = platform.machine().lower()
    if machine in ("amd64", "x86_64"):
        arch = "x86_64"
    elif machine in ("arm64", "aarch64"):
        arch = "arm64"
    else:
        arch = machine or "unknown"

    if system == "linux":
        return f"netdiff-linux-{arch}"
    if system == "darwin":
        return f"netdiff-macos-{arch}"
    if system == "windows":
        return f"netdiff-windows-{arch}"
    return f"netdiff-{system}-{arch}"


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def smoke_version(binary: Path) -> str:
    result = subprocess.run(
        [str(binary), "version"],
        check=True,
        capture_output=True,
        text=True,
    )
    text = (result.stdout or result.stderr or "").strip()
    if "netdiff" not in text.lower():
        raise SystemExit(f"unexpected version output: {text!r}")
    return text


def write_notice(dest: Path, version: str) -> None:
    dest.write_text(
        "NetDiff "
        + version
        + "\n\n"
        "Semantic connectivity diff for KiCad schematics.\n"
        "Run `netdiff version` to verify the install.\n\n"
        "Licensing for the engine fork must be resolved before commercial release\n"
        "(see docs/00_PRD.md §9). This binary is provided for evaluation and CI use.\n",
        encoding="utf-8",
    )


def make_archive(
    staging_dir: Path,
    out_archive: Path,
    fmt: str,
) -> None:
    """Pack staging contents at archive root (netdiff + NOTICE.txt)."""
    out_archive.parent.mkdir(parents=True, exist_ok=True)
    files = sorted(p for p in staging_dir.iterdir() if p.is_file())
    if fmt == "zip":
        with zipfile.ZipFile(out_archive, "w", compression=zipfile.ZIP_DEFLATED) as zf:
            for path in files:
                zf.write(path, arcname=path.name)
    else:
        with tarfile.open(out_archive, "w:gz") as tf:
            for path in files:
                tf.add(path, arcname=path.name)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path, help="Path to built netdiff binary")
    parser.add_argument("--name", default=None, help="Archive basename, e.g. netdiff-linux-x86_64")
    parser.add_argument("--version", default="0.1.0")
    parser.add_argument("--out", type=Path, default=Path("dist"))
    parser.add_argument(
        "--format",
        choices=("auto", "tar.gz", "zip"),
        default="auto",
        help="Archive format (auto: zip on Windows names, else tar.gz)",
    )
    args = parser.parse_args()

    binary = args.binary.resolve()
    if not binary.is_file():
        raise SystemExit(f"binary not found: {binary}")

    # Ensure +x on Unix so smoke and packaged installs work.
    if os.name != "nt":
        mode = binary.stat().st_mode
        binary.chmod(mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

    version_line = smoke_version(binary)
    print(f"smoke: {version_line}")

    name = args.name or detect_default_name()
    fmt = args.format
    if fmt == "auto":
        fmt = "zip" if "windows" in name.lower() or os.name == "nt" else "tar.gz"

    suffix = ".zip" if fmt == "zip" else ".tar.gz"
    out_archive = args.out / f"{name}{suffix}"
    out_sum = args.out / f"{name}.sha256"

    with tempfile.TemporaryDirectory(prefix="netdiff-pkg-") as tmp:
        staging = Path(tmp) / name
        staging.mkdir(parents=True)
        dest_name = "netdiff.exe" if "windows" in name.lower() else "netdiff"
        shutil.copy2(binary, staging / dest_name)
        if os.name != "nt" and not dest_name.endswith(".exe"):
            p = staging / dest_name
            p.chmod(p.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
        write_notice(staging / "NOTICE.txt", args.version)
        make_archive(staging, out_archive, fmt)

    digest = sha256_file(out_archive)
    out_sum.write_text(f"{digest}  {out_archive.name}\n", encoding="utf-8")
    print(f"wrote {out_archive}")
    print(f"wrote {out_sum}")
    print(f"sha256 {digest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
