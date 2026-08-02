#!/usr/bin/env python3
"""T2.4: plugin DiffResult → UI rows (no KiCad / wx required)."""

from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "plugin" / "plugins"))

from netdiff_kicad.model import group_by_type, significant_rows  # noqa: E402


def main() -> int:
    golden = ROOT / "tests" / "golden" / "diff_all_change_types.json"
    diff = json.loads(golden.read_text(encoding="utf-8"))
    rows = significant_rows(diff)
    if not rows:
        print("FAIL: expected significant rows from golden diff")
        return 1
    grouped = group_by_type(rows)
    if "PinConnectionChanged" not in grouped and "ComponentAdded" not in grouped:
        print(f"FAIL: unexpected groups {sorted(grouped)}")
        return 1
    # Navigation metadata present for component add.
    added = [r for r in rows if r.change_type == "ComponentAdded"]
    if not added or added[0].ref is None or added[0].x is None:
        print("FAIL: ComponentAdded row missing ref/position for navigation")
        return 1
    print(f"PASS: {len(rows)} significant rows, types={sorted(grouped)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
