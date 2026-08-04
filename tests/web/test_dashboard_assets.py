#!/usr/bin/env python3
"""Smoke: dashboard static assets exist and JS modules parse as UTF-8 text."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DASH = ROOT / "web" / "dashboard"
REQUIRED = [
    DASH / "index.html",
    DASH / "styles.css",
    DASH / "js" / "app.js",
    DASH / "js" / "wasm.js",
    DASH / "js" / "render.js",
    DASH / "js" / "history.js",
    DASH / "data" / "sample-diff.json",
    ROOT / "web" / "index.html",
    ROOT / "web" / "vercel.json",
]


def main() -> int:
    missing = [str(p) for p in REQUIRED if not p.is_file()]
    if missing:
        print("FAIL: missing " + ", ".join(missing))
        return 1
    app = (DASH / "js" / "app.js").read_text(encoding="utf-8")
    if "Run connectivity diff" not in (DASH / "index.html").read_text(encoding="utf-8"):
        print("FAIL: dashboard shell incomplete")
        return 1
    if "diffSchematicTexts" not in app or "localStorage" not in (DASH / "js" / "history.js").read_text(
        encoding="utf-8"
    ):
        print("FAIL: dashboard modules incomplete")
        return 1
    print("PASS: dashboard assets present")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
