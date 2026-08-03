#!/usr/bin/env python3
"""Acceptance gate for the netlist correctness oracle (docs/06_TESTING_QA.md §1).

This file is the contract. It must fail until:
  1. MANIFEST lists ≥8 corpus designs with source URL + sheet_count, majority multi-sheet
  2. `run_oracle.py --report <path>` writes a machine-readable JSON report
  3. The report schema includes per-design pass/fail + net/pin/component counts
  4. A failing design is not silently skipped (strict comparison)

Usage (after implementing --report):
  python tests/oracle/test_correctness_gate_contract.py
  python tests/oracle/run_oracle.py --report build/oracle-report.json
  python tests/oracle/test_correctness_gate_contract.py --report build/oracle-report.json

Exit: 0 pass, 1 fail contract / report, 2 usage.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "tests" / "corpus" / "MANIFEST.md"
ORACLE = ROOT / "tests" / "oracle" / "run_oracle.py"


def fail(msg: str) -> None:
    print(f"FAIL: {msg}")


def parse_manifest_sheet_table(text: str) -> list[dict]:
    """Expect a markdown table with columns including Project / sheet count / multi-sheet."""
    rows: list[dict] = []
    # Look for a dedicated sheet-count table first.
    for block in re.split(r"\n## ", text):
        if "sheet count" not in block.lower() and "sheets" not in block.lower():
            continue
        for line in block.splitlines():
            if not line.startswith("|"):
                continue
            cells = [c.strip() for c in line.strip("|").split("|")]
            if len(cells) < 3:
                continue
            if cells[0].lower() in {"project", "corpus id", "---"} or set(cells[0]) <= {"-"}:
                continue
            if "---" in cells[0]:
                continue
            # Heuristic: project, source, sheets, ...
            project = cells[0].strip("` ")
            sheet_cell = None
            for c in cells[1:]:
                if re.fullmatch(r"\d+", c):
                    sheet_cell = int(c)
                    break
            if sheet_cell is None:
                continue
            multi = any("yes" == c.lower() for c in cells[1:])
            rows.append({"id": project, "sheets": sheet_cell, "multi": multi or sheet_cell > 1})
    return rows


def check_manifest() -> list[str]:
    errors: list[str] = []
    if not MANIFEST.is_file():
        return ["MANIFEST.md missing"]
    text = MANIFEST.read_text(encoding="utf-8")
    rows = parse_manifest_sheet_table(text)
    if len(rows) < 8:
        errors.append(
            f"MANIFEST must list ≥8 designs with numeric sheet counts "
            f"(found {len(rows)} parsable rows). Add a '## Sheet counts' table."
        )
        return errors
    multi = sum(1 for r in rows if r["sheets"] > 1 or r["multi"])
    if multi * 2 <= len(rows):  # not MOST
        errors.append(
            f"MOST designs must be hierarchical/multi-sheet; "
            f"got {multi}/{len(rows)} with sheets>1"
        )
    for r in rows:
        if r["sheets"] < 1:
            errors.append(f"{r['id']}: invalid sheet_count {r['sheets']}")
    return errors


def check_oracle_supports_report() -> list[str]:
    text = ORACLE.read_text(encoding="utf-8")
    errors: list[str] = []
    if "--report" not in text:
        errors.append("run_oracle.py must accept --report <path> for machine-readable output")
    if "correctness" not in text.lower() and "report" not in text:
        errors.append("run_oracle.py must write a correctness report structure")
    return errors


def check_report(path: Path) -> list[str]:
    errors: list[str] = []
    if not path.is_file():
        return [f"report file missing: {path}"]
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        return [f"report is not JSON: {exc}"]

    for key in ("schema_version", "passed", "failed", "designs"):
        if key not in data:
            errors.append(f"report missing top-level key '{key}'")
    designs = data.get("designs")
    if not isinstance(designs, list) or len(designs) < 8:
        errors.append(f"report.designs must be a list of ≥8 entries (got {type(designs).__name__})")
        return errors

    required = {
        "id",
        "status",
        "sheet_count",
        "components_kicad",
        "components_netdiff",
        "nets_kicad",
        "nets_netdiff",
        "pins_kicad",
        "pins_netdiff",
    }
    fails = 0
    for d in designs:
        missing = required - set(d)
        if missing:
            errors.append(f"design {d.get('id','?')} missing fields {sorted(missing)}")
            continue
        if d["status"] not in {"PASS", "FAIL"}:
            errors.append(f"{d['id']}: status must be PASS|FAIL, got {d['status']!r}")
        if d["status"] == "FAIL":
            fails += 1
            if not d.get("errors"):
                errors.append(f"{d['id']}: FAIL must include non-empty 'errors' (do not hide diffs)")
    # Contract: if any design failed, report.failed must be > 0 and passed false
    if fails and (data.get("failed", 0) < 1 or data.get("passed") is True):
        errors.append("report.passed/failed disagree with per-design FAIL statuses")
    return errors


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--report",
        type=Path,
        default=None,
        help="If set, validate an existing oracle JSON report (full gate).",
    )
    ap.add_argument(
        "--require-report-file",
        action="store_true",
        help="Fail if --report is omitted (use in CI after generating the report).",
    )
    args = ap.parse_args(argv)

    errors: list[str] = []
    errors.extend(check_manifest())
    errors.extend(check_oracle_supports_report())

    if args.require_report_file and args.report is None:
        errors.append("--require-report-file set but no --report path given")
    if args.report is not None:
        errors.extend(check_report(args.report))
    elif args.require_report_file:
        pass
    else:
        # Phase A of TDD: structural contract only (expect FAIL until MANIFEST+--report exist).
        print("NOTE: no --report yet; checking structural contract only.")

    if errors:
        for e in errors:
            fail(e)
        print(f"\nCONTRACT FAILED ({len(errors)} issue(s))")
        return 1
    print("CONTRACT PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
