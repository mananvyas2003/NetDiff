#!/usr/bin/env python3
"""Validate netdiff SARIF output against the vendored SARIF 2.1.0 schema.

Acceptance criterion for T1.6 / docs/06_TESTING_QA.md §6. Runs fully offline:
the schema is vendored in tests/schema/ (see its README for provenance).

Usage: python tests/format/validate_sarif.py [sarif-file ...]
Defaults to tests/golden/format_sample.sarif.json.

Exit codes: 0 valid, 1 invalid, 2 harness problem (missing schema/dependency).
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCHEMA = ROOT / "tests" / "schema" / "sarif-2.1.0.json"
DEFAULT_TARGET = ROOT / "tests" / "golden" / "format_sample.sarif.json"


def main(argv: list[str]) -> int:
    try:
        import jsonschema
    except ImportError:
        print("FAIL: jsonschema is not installed (pip install jsonschema)", file=sys.stderr)
        return 2

    if not SCHEMA.is_file():
        print(f"FAIL: vendored schema missing: {SCHEMA}", file=sys.stderr)
        return 2

    schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
    validator = jsonschema.Draft7Validator(schema)

    targets = [Path(a) for a in argv[1:]] or [DEFAULT_TARGET]
    failed = 0
    for target in targets:
        if not target.is_file():
            print(f"FAIL: {target} does not exist", file=sys.stderr)
            failed += 1
            continue
        document = json.loads(target.read_text(encoding="utf-8"))
        errors = sorted(validator.iter_errors(document), key=lambda e: list(e.path))
        if errors:
            failed += 1
            print(f"FAIL: {target.name} is not valid SARIF 2.1.0 ({len(errors)} error(s))")
            for error in errors[:10]:
                location = "/".join(str(p) for p in error.path) or "<root>"
                print(f"   at {location}: {error.message}")
        else:
            runs = document.get("runs", [])
            results = sum(len(r.get("results", [])) for r in runs)
            print(f"PASS: {target.name} is valid SARIF 2.1.0 "
                  f"({len(runs)} run(s), {results} result(s))")

    if failed:
        print(f"\n{failed} file(s) failed validation")
        return 1
    print("\nSARIF validation: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
