#!/usr/bin/env python3
"""Acceptance: --format html visual review on real multi-sheet corpus designs.

For 3 hierarchical projects with known electrical edits, the HTML must let a
human identify every changed net/component without reading JSON:
  - self-contained / offline (no external network URLs)
  - significant vs cosmetic grouping
  - sheet path visible per change (hierarchical)
  - changed nets/pins/components appear as labeled highlighted SVG/HTML nodes

Usage: python tests/cli/test_html_visual_review.py <path-to-netdiff>
Exit: 0 pass, 1 fail, 2 harness error.
"""

from __future__ import annotations

import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CORPUS = ROOT / "tests" / "corpus"

# Real multi-sheet designs (sheet_count > 1 in MANIFEST).
CASES = [
    ("complex_hierarchy", "complex_hierarchy/complex_hierarchy.kicad_sch"),
    ("pic_programmer", "pic_programmer/pic_programmer.kicad_sch"),
    ("multichannel", "multichannel/multichannel_mixer.kicad_sch"),
]


def run(binary: str, args: list[str], cwd: Path | None = None) -> subprocess.CompletedProcess:
    return subprocess.run(
        [binary, *args],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        cwd=cwd,
    )


def top_level_blocks(text: str) -> list[tuple[int, int]]:
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


def mutate_first_value(sch_text: str) -> tuple[str, str, str]:
    """Flip first instance Value property (skip lib_symbols — not connectivity)."""
    skip: list[tuple[int, int]] = []
    for start, end in top_level_blocks(sch_text):
        if sch_text.startswith("(lib_symbols", start):
            skip.append((start, end))

    def in_skip(pos: int) -> bool:
        return any(a <= pos < b for a, b in skip)

    for m in re.finditer(r'\(property "Value" "([^"]*)"', sch_text):
        if in_skip(m.start()):
            continue
        old = m.group(1)
        new = "NETDIFF_HTML_VISUAL_PROBE"
        if old == new:
            new = "NETDIFF_HTML_VISUAL_PROBE_2"
        after = sch_text[: m.start(1)] + new + sch_text[m.end(1) :]
        return old, new, after
    raise RuntimeError("no instance Value property found to mutate")


def changed_subjects_from_json(diff: dict) -> dict[str, set[str]]:
    """Collect visible labels we expect in the HTML, by significance."""
    out = {"SIGNIFICANT": set(), "COSMETIC": set()}
    for change in diff.get("changes") or []:
        sig = change.get("significance") or "SIGNIFICANT"
        bucket = out.setdefault(sig, set())
        payload = change.get("payload") or {}
        ctype = change.get("type") or ""
        if ctype == "ComponentModified":
            bucket.add(payload.get("ref") or "")
            for fc in payload.get("changes") or []:
                if fc.get("after"):
                    bucket.add(str(fc["after"]))
        elif ctype == "ComponentAdded":
            c = payload.get("component") or {}
            bucket.add(c.get("ref") or "")
        elif ctype == "ComponentRemoved":
            c = payload.get("component") or {}
            bucket.add(c.get("ref") or "")
        elif ctype in {"NetAdded", "NetRemoved"}:
            n = payload.get("net") or {}
            bucket.add(n.get("name") or "")
        elif ctype == "NetRenamed":
            bucket.add(payload.get("before_name") or "")
            bucket.add(payload.get("after_name") or "")
        elif ctype == "NetMerged":
            for n in payload.get("before_nets") or []:
                bucket.add(n)
            bucket.add(payload.get("after_net") or "")
        elif ctype == "NetSplit":
            bucket.add(payload.get("before_net") or "")
            for n in payload.get("after_nets") or []:
                bucket.add(n)
        elif ctype == "PinConnectionChanged":
            bucket.add(payload.get("pin") or "")
            bucket.add(payload.get("before_net") or "")
            bucket.add(payload.get("after_net") or "")
        msg = change.get("message") or ""
        if msg:
            bucket.add(msg)
    for k in list(out):
        out[k] = {x for x in out[k] if x}
    return out


def assert_offline(html: str) -> list[str]:
    errors = []
    # Ban external resource loads (security selling point).
    for pat, label in [
        (r"https?://", "http(s) URL"),
        (r"//cdn\.", "protocol-relative CDN"),
        (r"src\s*=\s*\"//", "protocol-relative src"),
    ]:
        if re.search(pat, html, re.I):
            # Allow only in comments? Still ban — fully offline.
            errors.append(f"external reference found ({label})")
    return errors


def assert_visual(html: str, subjects: dict[str, set[str]], design: str) -> list[str]:
    errors = []
    if "<svg" not in html.lower():
        errors.append(f"{design}: HTML must include an SVG net/change graph")
    if "significant" not in html.lower():
        errors.append(f"{design}: missing Significant grouping")
    # Sheet markers for hierarchical review.
    if "data-sheet=" not in html and "sheet:" not in html.lower() and "sheet_path" not in html:
        errors.append(f"{design}: missing sheet path markers for hierarchical review")

    # Every significant subject must appear as a labeled highlight the eye can find.
    for label in subjects.get("SIGNIFICANT") or []:
        # Prefer dedicated visual hooks; fall back to visible text.
        hook = f'data-subject="{label}"'
        if hook not in html and label not in html:
            errors.append(f"{design}: significant change not visually present: {label!r}")

    if "highlight" not in html.lower() and "changed" not in html.lower():
        errors.append(f"{design}: no highlight/changed styling hooks for the eye")
    return errors


def exercise(binary: str, work: Path, cid: str, rel: str) -> list[str]:
    errors: list[str] = []
    src = CORPUS / rel
    if not src.is_file():
        return [f"missing corpus entry {src}"]

    project = work / cid
    shutil.copytree(src.parent, project)
    entry_name = Path(rel).name
    entry = project / entry_name
    original = entry.read_text(encoding="utf-8", errors="replace")
    _old, new_val, mutated = mutate_first_value(original)
    entry.write_text(mutated, encoding="utf-8")

    before = src  # pristine corpus
    after = entry

    html_path = work / f"{cid}.html"
    json_path = work / f"{cid}.json"

    r_json = run(
        binary,
        [
            "diff",
            str(before),
            str(after),
            "--format",
            "json",
            "--output",
            str(json_path),
            "--no-color",
            "--include-cosmetic",
        ],
    )
    # Gate fail (1) is expected for electrical edit.
    if r_json.returncode not in (0, 1):
        return [f"{cid}: json diff failed rc={r_json.returncode}: {r_json.stderr}"]

    r_html = run(
        binary,
        [
            "diff",
            str(before),
            str(after),
            "--format",
            "html",
            "--output",
            str(html_path),
            "--no-color",
            "--include-cosmetic",
        ],
    )
    if r_html.returncode == 2 and "unknown --format" in (r_html.stderr + r_html.stdout):
        return [
            f"{cid}: --format html not implemented yet (rc=2). "
            "This acceptance test must fail until the formatter lands."
        ]
    if r_html.returncode not in (0, 1):
        return [f"{cid}: html diff failed rc={r_html.returncode}: {r_html.stderr}"]
    if not html_path.is_file() or html_path.stat().st_size < 200:
        return [f"{cid}: HTML output missing or too small"]

    html = html_path.read_text(encoding="utf-8", errors="replace")
    diff = json.loads(json_path.read_text(encoding="utf-8"))
    if (diff.get("summary") or {}).get("significant_count", 0) < 1:
        errors.append(
            f"{cid}: expected significant_count>=1 after value mutate to {new_val!r}"
        )
    subjects = changed_subjects_from_json(diff)
    errors.extend(assert_offline(html))
    errors.extend(assert_visual(html, subjects, cid))

    # Probe value must be findable in the visual document.
    if new_val not in html and not any(new_val in s for s in subjects.get("SIGNIFICANT", [])):
        # If JSON captured it via field after, subjects check covers it; else fail.
        if new_val not in html:
            errors.append(f"{cid}: mutated value {new_val!r} not visible in HTML")

    return errors


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print("usage: test_html_visual_review.py <netdiff-binary>", file=sys.stderr)
        return 2
    binary = str(Path(argv[1]).resolve())
    if not Path(binary).is_file():
        print(f"missing binary: {binary}", file=sys.stderr)
        return 2

    all_errors: list[str] = []
    with tempfile.TemporaryDirectory(prefix="netdiff-html-") as tmp:
        work = Path(tmp)
        for cid, rel in CASES:
            print(f"=== {cid} ===")
            errs = exercise(binary, work, cid, rel)
            if errs:
                for e in errs:
                    print(f"FAIL: {e}")
                all_errors.extend(errs)
            else:
                print("PASS")

    if all_errors:
        print(f"\nFAILED: {len(all_errors)} check(s)")
        return 1
    print("\nALL PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
