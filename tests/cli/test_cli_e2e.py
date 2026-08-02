#!/usr/bin/env python3
"""T1.7 end-to-end: the netdiff CLI against a real two-commit git repository.

Covers every command, the exit-code contract (04_CLI_CI_SPEC.md §1.3), --git,
--staged, config resolution and the output formats.

Usage: python tests/cli/test_cli_e2e.py <path-to-netdiff-binary>
Exit: 0 all checks pass, 1 a check failed, 2 harness problem.
"""

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CORPUS = ROOT / "tests" / "corpus"
SOURCE_PROJECT = CORPUS / "ecc83"
ENTRY_NAME = "ecc83-pp.kicad_sch"

failures = 0


def check(condition: bool, what: str) -> None:
    global failures
    if not condition:
        print(f"FAIL: {what}")
        failures += 1


def run(binary: str, args: list[str], cwd: Path | None = None):
    return subprocess.run([binary, *args], capture_output=True, text=True,
                          encoding="utf-8", errors="replace", cwd=cwd)


def git(repo: Path, *args: str) -> None:
    subprocess.run(["git", *args], cwd=repo, check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def top_level_blocks(text: str) -> list[tuple[int, int]]:
    """Direct children of the root s-expression, as (start, end) offsets."""
    root = text.find("(")
    depth, start, in_str, blocks, i = 0, None, False, [], root
    while i < len(text):
        c = text[i]
        if in_str:
            if c == chr(92):
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
    """Move every placed coordinate: cosmetic, connectivity-preserving.

    `lib_symbols` is skipped: its coordinates are relative to each symbol's own
    origin, so shifting them would move pins off their wires. The block is found
    by scanning parens rather than by matching line prefixes, because the corpus
    is CRLF and a "\\n\\t(symbol" style search silently finds nothing there.
    """
    def shift(m: re.Match) -> str:
        return f"({m.group(1)} {float(m.group(2)) + dx:g}{m.group(3)}{m.group(4)}"

    pattern = r"\((at|xy) ([-+]?[0-9]*\.?[0-9]+)(\s+)([-+]?[0-9]*\.?[0-9]+)"
    for start, end in top_level_blocks(text):
        if text.startswith("(lib_symbols", start):
            return (re.sub(pattern, shift, text[:start]) + text[start:end] +
                    re.sub(pattern, shift, text[end:]))
    return re.sub(pattern, shift, text)


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print("usage: test_cli_e2e.py <netdiff-binary>", file=sys.stderr)
        return 2
    # Absolute: subprocesses below run with cwd set elsewhere.
    binary = str(Path(argv[1]).resolve())
    if not Path(binary).is_file():
        print(f"FAIL: no such binary: {binary}", file=sys.stderr)
        return 2
    if shutil.which("git") is None:
        print("FAIL: git is required for the CLI end-to-end test", file=sys.stderr)
        return 2

    work = Path(tempfile.mkdtemp(prefix="netdiff-e2e-"))
    try:
        # ---- a repository with the design committed twice --------------------
        repo = work / "repo"
        repo.mkdir()
        git(repo, "init", "-q")
        git(repo, "config", "user.email", "test@example.com")
        git(repo, "config", "user.name", "netdiff test")

        project = repo / "hardware"
        shutil.copytree(SOURCE_PROJECT, project)
        entry = project / ENTRY_NAME
        git(repo, "add", "-A")
        git(repo, "commit", "-q", "-m", "initial")

        original = entry.read_text(encoding="utf-8", errors="replace")

        # Commit 2: a real electrical change (swap a resistor value).
        mutated, count = re.subn(r'\(property "Value" "1.5K"',
                                 '(property "Value" "2.2K"', original, count=1)
        check(count == 1, "fixture: the value mutation applied")
        entry.write_text(mutated, encoding="utf-8")
        git(repo, "add", "-A")
        git(repo, "commit", "-q", "-m", "change a resistor")

        # ---- version / help --------------------------------------------------
        r = run(binary, ["version"])
        check(r.returncode == 0 and "netdiff" in r.stdout, "version exits 0 and prints a name")

        # ---- validate --------------------------------------------------------
        r = run(binary, ["validate", str(project)])
        check(r.returncode == 0 and "ok:" in r.stdout,
              f"validate exits 0 on a good project (got {r.returncode})")

        # ---- graph -----------------------------------------------------------
        r = run(binary, ["graph", str(entry)])
        check(r.returncode == 0, f"graph exits 0 (got {r.returncode})")
        try:
            graph = json.loads(r.stdout)
            check(graph["schema_version"] == "1.0" and len(graph["components"]) > 0,
                  "graph emits a populated ConnectivityGraph")
        except json.JSONDecodeError:
            check(False, "graph emits valid JSON")

        # ---- diff --git: a connectivity/value change gates ---------------------
        r = run(binary, ["diff", "--git", "HEAD~1", "HEAD", str(project), "--no-color"])
        check(r.returncode == 1, f"diff --git exits 1 when the gate fails (got {r.returncode})")
        check("2.2K" in r.stdout, "diff --git reports the changed value")

        # --fail-on never keeps the same findings but stops gating.
        r = run(binary, ["diff", "--git", "HEAD~1", "HEAD", str(project),
                         "--fail-on", "never", "--no-color"])
        check(r.returncode == 0, f"--fail-on never exits 0 (got {r.returncode})")
        check("2.2K" in r.stdout, "--fail-on never still reports the change")

        # Reversed refs still work (and are still a change).
        r = run(binary, ["diff", "--git", "HEAD", "HEAD~1", str(project), "--no-color"])
        check(r.returncode == 1, "diff --git works in the other direction")

        # ---- diff --git on a cosmetic-only commit ----------------------------
        entry.write_text(translate(entry.read_text(encoding="utf-8",
                                                   errors="replace"), 25.4),
                         encoding="utf-8")
        git(repo, "add", "-A")
        git(repo, "commit", "-q", "-m", "move things around")
        r = run(binary, ["diff", "--git", "HEAD~1", "HEAD", str(project), "--no-color"])
        check(r.returncode == 0,
              f"a cosmetic-only commit passes the gate (got {r.returncode}): {r.stdout.strip()}")
        check("No electrical changes." in r.stdout,
              "a cosmetic-only commit says there are no electrical changes")

        # ---- diff --staged ---------------------------------------------------
        r = run(binary, ["diff", "--staged", str(project), "--no-color"])
        check(r.returncode == 0, "clean working tree: --staged exits 0")

        text = entry.read_text(encoding="utf-8", errors="replace")
        entry.write_text(text.replace('(property "Value" "2.2K"',
                                      '(property "Value" "9.9K"', 1), encoding="utf-8")
        r = run(binary, ["diff", "--staged", str(project), "--no-color"])
        check(r.returncode == 1, f"dirty working tree: --staged exits 1 (got {r.returncode})")
        check("9.9K" in r.stdout, "--staged reports the uncommitted change")
        entry.write_text(text, encoding="utf-8")

        # ---- file-vs-file diff -----------------------------------------------
        other = work / "copy"
        shutil.copytree(project, other)
        r = run(binary, ["diff", str(project), str(other), "--no-color"])
        check(r.returncode == 0, "identical directories diff clean")

        # ---- formats ----------------------------------------------------------
        r = run(binary, ["diff", "--git", "HEAD~2", "HEAD~1", str(project),
                         "--format", "json"])
        check(r.returncode == 1, "json format still honours the gate")
        try:
            result = json.loads(r.stdout)
            check(result["summary"]["gate"] == "FAIL", "json carries the gate verdict")
            check(result["schema_version"] == "1.0", "json carries the schema version")
        except json.JSONDecodeError:
            check(False, "json format emits valid JSON")

        r = run(binary, ["diff", "--git", "HEAD~2", "HEAD~1", str(project),
                         "--format", "markdown"])
        check("### NetDiff" in r.stdout, "markdown format emits a heading")

        sarif_path = work / "out.sarif"
        r = run(binary, ["diff", "--git", "HEAD~2", "HEAD~1", str(project),
                         "--format", "sarif", "--output", str(sarif_path)])
        check(sarif_path.is_file(), "--output writes the file")
        check(r.stdout.strip() == "", "--output keeps stdout clean")
        try:
            sarif = json.loads(sarif_path.read_text(encoding="utf-8"))
            check(sarif["version"] == "2.1.0", "sarif output declares version 2.1.0")
            check(len(sarif["runs"][0]["results"]) > 0, "sarif output has results")
        except (json.JSONDecodeError, KeyError, IndexError):
            check(False, "sarif output is well-formed")

        # ---- config file resolution -------------------------------------------
        (repo / ".netdiff.yml").write_text(
            "schema_version: \"1.0\"\n"
            "gate:\n"
            "  fail_on: never\n", encoding="utf-8")
        r = run(binary, ["diff", "--git", "HEAD~2", "HEAD~1", str(project), "--no-color"])
        check(r.returncode == 0,
              f".netdiff.yml found by walking up disables the gate (got {r.returncode})")

        # An explicit flag still beats the file (04 §4).
        r = run(binary, ["diff", "--git", "HEAD~2", "HEAD~1", str(project),
                         "--fail-on", "significant", "--no-color"])
        check(r.returncode == 1, "--fail-on overrides the config file")

        bad_config = work / "bad.yml"
        bad_config.write_text("gate:\n  fail_on: whenever\n", encoding="utf-8")
        r = run(binary, ["diff", "--git", "HEAD~2", "HEAD~1", str(project),
                         "--config", str(bad_config)])
        check(r.returncode == 2, f"an invalid config is a usage error (got {r.returncode})")
        (repo / ".netdiff.yml").unlink()

        # ---- exit code 3: parse errors -----------------------------------------
        broken_dir = work / "broken"
        broken_dir.mkdir()
        (broken_dir / "broken.kicad_sch").write_text(
            "(kicad_sch (version 20250114) (this is not balanced",
            encoding="utf-8")
        r = run(binary, ["validate", str(broken_dir)])
        check(r.returncode == 3,
              f"validate on a malformed sheet exits 3 (got {r.returncode})")
        check("netdiff: error:" in r.stderr, "parse failure explains itself on stderr")

        r = run(binary, ["graph", str(broken_dir / "broken.kicad_sch")])
        check(r.returncode == 3, f"graph on a malformed sheet exits 3 (got {r.returncode})")

        empty = work / "empty.kicad_sch"
        empty.write_text("", encoding="utf-8")
        r = run(binary, ["graph", str(empty)])
        check(r.returncode == 3, f"graph on an empty file exits 3 (got {r.returncode})")

        # ---- exit code 2: usage errors -----------------------------------------
        for args, label in [
            ([], "no arguments"),
            (["frobnicate"], "unknown command"),
            (["diff"], "diff without operands"),
            (["diff", str(entry)], "diff with one operand"),
            (["graph"], "graph without a schematic"),
            (["diff", "--git", "HEAD"], "--git without both refs"),
            (["diff", str(entry), str(entry), "--format", "yaml"], "unknown format"),
            (["diff", str(entry), str(entry), "--fail-on", "maybe"], "unknown fail-on"),
            (["diff", str(entry), str(entry), "--bogus"], "unknown option"),
            (["graph", str(work / "does-not-exist.kicad_sch")], "missing file"),
            (["diff", "--git", "HEAD~1", "HEAD", str(work)], "path outside a repository"),
        ]:
            r = run(binary, args)
            check(r.returncode == 2, f"usage error exits 2: {label} (got {r.returncode})")

        # A ref that could be read as an option must be refused, not passed to git.
        r = run(binary, ["diff", "--git", "--upload-pack=touch", "HEAD", str(project)])
        check(r.returncode in (2, 3),
              f"a hostile-looking ref is refused (got {r.returncode})")

        # ---- a nonexistent revision is a parse/resolve error, not a crash -------
        r = run(binary, ["diff", "--git", "deadbeef", "HEAD", str(project)])
        check(r.returncode == 3, f"unknown revision exits 3 (got {r.returncode})")

    finally:
        shutil.rmtree(work, ignore_errors=True)

    if failures:
        print(f"\n{failures} check(s) failed")
        return 1
    print("cli end-to-end: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
