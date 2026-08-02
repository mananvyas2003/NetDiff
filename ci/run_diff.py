#!/usr/bin/env python3
"""Run netdiff for a PR and write markdown + SARIF (04_CLI_CI_SPEC.md §2.1).

Env:
  NETDIFF_BIN, BASE_SHA, HEAD_SHA, PROJECT_PATH, CONFIG_PATH, FAIL_ON, OUT_DIR
  GITHUB_OUTPUT (optional)

Always exits 0 so the composite action can comment / upload before gating.
"""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path


def append_output(key: str, value: str) -> None:
    path = os.environ.get("GITHUB_OUTPUT")
    if not path:
        return
    with open(path, "a", encoding="utf-8") as f:
        f.write(f"{key}={value}\n")


def run_netdiff(binary: str, args: list[str]) -> int:
    proc = subprocess.run([binary, *args], capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    if proc.stdout:
        sys.stdout.write(proc.stdout)
    if proc.stderr:
        sys.stderr.write(proc.stderr)
    return proc.returncode


def main() -> int:
    binary = os.environ["NETDIFF_BIN"]
    base = os.environ["BASE_SHA"]
    head = os.environ["HEAD_SHA"]
    project = os.environ.get("PROJECT_PATH") or "."
    config = os.environ.get("CONFIG_PATH") or ""
    fail_on = os.environ.get("FAIL_ON") or ""
    out_dir = Path(os.environ["OUT_DIR"])
    out_dir.mkdir(parents=True, exist_ok=True)

    base_args = ["diff", "--git", base, head, project]
    if config:
        base_args += ["--config", config]
    if fail_on:
        base_args += ["--fail-on", fail_on]

    md_path = out_dir / "report.md"
    sarif_path = out_dir / "netdiff.sarif"

    md_rc = run_netdiff(binary, base_args + ["--format", "markdown",
                                             "--output", str(md_path),
                                             "--no-color"])
    sarif_rc = run_netdiff(binary, base_args + ["--format", "sarif",
                                                "--output", str(sarif_path),
                                                "--no-color"])
    rc = max(md_rc, sarif_rc)

    if not md_path.is_file() or md_path.stat().st_size == 0:
        if rc == 0:
            md_path.write_text("# NetDiff\n\nNo electrical changes.\n", encoding="utf-8")
        else:
            md_path.write_text(
                f"# NetDiff\n\nDiff failed (exit {rc}). See workflow logs.\n",
                encoding="utf-8",
            )

    (out_dir / "exit_code").write_text(str(rc), encoding="utf-8")
    append_output("exit_code", str(rc))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyError as exc:
        print(f"missing env: {exc}", file=sys.stderr)
        raise SystemExit(2) from exc
