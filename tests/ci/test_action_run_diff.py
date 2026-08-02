#!/usr/bin/env python3
"""T2.2 smoke: ci/run_diff.py against a two-commit schematic repo.

Usage: python tests/ci/test_action_run_diff.py <netdiff-binary>
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "tests" / "corpus" / "ecc83"
ENTRY = "ecc83-pp.kicad_sch"
RUN_DIFF = ROOT / "ci" / "run_diff.py"


def git(repo: Path, *args: str) -> str:
    return subprocess.check_output(["git", *args], cwd=repo, text=True).strip()


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print("usage: test_action_run_diff.py <netdiff-binary>", file=sys.stderr)
        return 2
    binary = Path(argv[1]).resolve()
    if not binary.is_file():
        print(f"missing binary: {binary}", file=sys.stderr)
        return 2
    if not RUN_DIFF.is_file():
        print(f"missing {RUN_DIFF}", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory(prefix="netdiff-action-") as tmp:
        repo = Path(tmp) / "repo"
        out = Path(tmp) / "out"
        shutil.copytree(SOURCE, repo)
        git(repo, "init")
        git(repo, "config", "user.email", "netdiff@example.com")
        git(repo, "config", "user.name", "NetDiff")
        git(repo, "add", "-A")
        git(repo, "commit", "-m", "base")
        base = git(repo, "rev-parse", "HEAD")

        sch = repo / ENTRY
        text = sch.read_text(encoding="utf-8")
        sch.write_text(text + "\n", encoding="utf-8")
        git(repo, "add", "-A")
        git(repo, "commit", "-m", "head")
        head = git(repo, "rev-parse", "HEAD")

        env = os.environ.copy()
        env.update(
            {
                "NETDIFF_BIN": str(binary),
                "BASE_SHA": base,
                "HEAD_SHA": head,
                "PROJECT_PATH": ".",
                "OUT_DIR": str(out),
                "GITHUB_OUTPUT": str(Path(tmp) / "github_output"),
            }
        )
        subprocess.run([sys.executable, str(RUN_DIFF)], cwd=repo, env=env, check=True)

        report = out / "report.md"
        exit_file = out / "exit_code"
        sarif = out / "netdiff.sarif"
        if not report.is_file() or report.stat().st_size == 0:
            print("FAIL: report.md missing/empty")
            return 1
        if not exit_file.is_file():
            print("FAIL: exit_code missing")
            return 1
        if not sarif.is_file():
            print("FAIL: netdiff.sarif missing")
            return 1
        code = exit_file.read_text(encoding="utf-8").strip()
        if code not in {"0", "1", "2", "3", "4"}:
            print(f"FAIL: bad exit_code {code!r}")
            return 1
        print(f"PASS: run_diff.py ok (exit_code={code}, report={report.stat().st_size}B)")
        return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
