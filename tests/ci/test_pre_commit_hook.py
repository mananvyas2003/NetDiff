#!/usr/bin/env python3
"""T2.3 smoke: pre-commit entry blocks significant staged changes and allows clean.

Usage: python tests/ci/test_pre_commit_hook.py <netdiff-binary>
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "tests" / "corpus" / "ecc83"
ENTRY = "ecc83-pp.kicad_sch"
HOOK = ROOT / "ci" / "pre_commit_netdiff.py"


def git(repo: Path, *args: str, check: bool = True) -> subprocess.CompletedProcess:
    return subprocess.run(
        ["git", *args],
        cwd=repo,
        check=check,
        capture_output=True,
        text=True,
    )


def top_level_blocks(text: str):
    root = text.find("(")
    depth, start, in_str, blocks, i = 0, None, False, [], root
    while i < len(text):
        c = text[i]
        if in_str:
            if c == "\\":
                i += 1
            elif c == '"':
                in_str = False
            i += 1
            continue
        if c == '"':
            in_str = True
        elif c == "(":
            depth += 1
            if depth == 2:
                start = i
        elif c == ")":
            if depth == 2 and start is not None:
                blocks.append((start, i + 1))
                start = None
            depth -= 1
            if depth == 0:
                break
        i += 1
    return blocks


def translate(text: str, dx: float) -> str:
    def shift(m: re.Match) -> str:
        return f"({m.group(1)} {float(m.group(2)) + dx:g}{m.group(3)}{m.group(4)}"

    pattern = r"\((at|xy) ([-+]?[0-9]*\.?[0-9]+)(\s+)([-+]?[0-9]*\.?[0-9]+)"
    for start, end in top_level_blocks(text):
        if text.startswith("(lib_symbols", start):
            return (
                re.sub(pattern, shift, text[:start])
                + text[start:end]
                + re.sub(pattern, shift, text[end:])
            )
    return re.sub(pattern, shift, text)


def run_hook(repo: Path, binary: Path) -> int:
    env = os.environ.copy()
    env["NETDIFF_BIN"] = str(binary)
    return subprocess.run(
        [sys.executable, str(HOOK)],
        cwd=repo,
        env=env,
    ).returncode


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print("usage: test_pre_commit_hook.py <netdiff-binary>", file=sys.stderr)
        return 2
    binary = Path(argv[1]).resolve()
    if not binary.is_file():
        print(f"missing binary: {binary}", file=sys.stderr)
        return 2

    failures = 0
    with tempfile.TemporaryDirectory(prefix="netdiff-pc-") as tmp:
        repo = Path(tmp) / "repo"
        shutil.copytree(SOURCE, repo)
        git(repo, "init")
        git(repo, "config", "user.email", "netdiff@example.com")
        git(repo, "config", "user.name", "NetDiff")
        git(repo, "add", "-A")
        git(repo, "commit", "-m", "base")

        # Clean working tree vs HEAD → gate pass.
        rc = run_hook(repo, binary)
        if rc != 0:
            print(f"FAIL: clean tree expected exit 0, got {rc}")
            failures += 1
        else:
            print("PASS: clean --staged exits 0")

        # Cosmetic move: significant_count should stay 0.
        sch = repo / ENTRY
        sch.write_text(translate(sch.read_text(encoding="utf-8"), 10.0), encoding="utf-8")
        git(repo, "add", "-A")
        rc = run_hook(repo, binary)
        if rc != 0:
            print(f"FAIL: cosmetic staged change expected exit 0, got {rc}")
            failures += 1
        else:
            print("PASS: cosmetic --staged exits 0")

        # Electrical change: resistor value (same mutation as tests/cli/test_cli_e2e.py).
        # --staged compares working tree vs HEAD (04 §1.5), so no git-add required.
        text = sch.read_text(encoding="utf-8")
        mutated, count = re.subn(
            r'\(property "Value" "1\.5K"',
            '(property "Value" "2.2K"',
            text,
            count=1,
        )
        if count != 1:
            print("FAIL: fixture value mutation did not apply")
            return 1
        sch.write_text(mutated, encoding="utf-8")
        rc = run_hook(repo, binary)
        if rc != 1:
            print(f"FAIL: electrical working-tree change expected exit 1, got {rc}")
            failures += 1
        else:
            print("PASS: electrical --staged exits 1 (blocks commit)")

    # YAML hook manifest exists and mentions the entry.
    hooks = (ROOT / "ci" / ".pre-commit-hooks.yaml").read_text(encoding="utf-8")
    if "id: netdiff" not in hooks or "pre_commit_netdiff.py" not in hooks:
        print("FAIL: .pre-commit-hooks.yaml incomplete")
        failures += 1
    else:
        print("PASS: .pre-commit-hooks.yaml present")

    gitlab = (ROOT / "ci" / "gitlab-netdiff.yml").read_text(encoding="utf-8")
    if "netdiff diff" not in gitlab and "diff --git" not in gitlab:
        print("FAIL: gitlab-netdiff.yml missing diff invocation")
        failures += 1
    else:
        print("PASS: gitlab-netdiff.yml present")

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
