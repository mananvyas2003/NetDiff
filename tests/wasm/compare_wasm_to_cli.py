#!/usr/bin/env python3
"""T2.5: compare WASM netdiff_diff_paths JSON gate to the native CLI.

Requires Node.js and a built web/demo/wasm/netdiff.js.
Usage:
  python tests/wasm/compare_wasm_to_cli.py --cli build/cli/netdiff \\
    --wasm-js web/demo/wasm/netdiff.js \\
    --before a.kicad_sch --after b.kicad_sch
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


NODE_SCRIPT = r"""
const fs = require('fs');
const path = require('path');
const createModule = require(process.argv[2]);
const beforePath = process.argv[3];
const afterPath = process.argv[4];
const beforeText = fs.readFileSync(beforePath, 'utf8');
const afterText = fs.readFileSync(afterPath, 'utf8');

createModule().then((Module) => {
  try { Module.FS.mkdir('/before'); } catch (e) {}
  try { Module.FS.mkdir('/after'); } catch (e) {}
  Module.FS.writeFile('/before/schematic.kicad_sch', beforeText);
  Module.FS.writeFile('/after/schematic.kicad_sch', afterText);
  const ptr = Module.ccall(
    'netdiff_diff_paths', 'number', ['string', 'string'],
    ['/before/schematic.kicad_sch', '/after/schematic.kicad_sch']);
  const json = Module.UTF8ToString(ptr);
  Module.ccall('netdiff_free', null, ['number'], [ptr]);
  process.stdout.write(json);
}).catch((err) => {
  console.error(err);
  process.exit(2);
});
"""


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cli", required=True, type=Path)
    parser.add_argument("--wasm-js", required=True, type=Path)
    parser.add_argument("--before", required=True, type=Path)
    parser.add_argument("--after", required=True, type=Path)
    args = parser.parse_args()

    if not args.wasm_js.is_file():
        print(f"SKIP: WASM js missing ({args.wasm_js}) — build with emcmake first")
        return 0
    if shutil.which("node") is None:
        print("SKIP: node not found")
        return 0
    if not args.cli.is_file():
        print(f"FAIL: cli missing {args.cli}")
        return 1

    cli = subprocess.run(
        [
            str(args.cli),
            "diff",
            str(args.before),
            str(args.after),
            "--format",
            "json",
            "--no-color",
        ],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    # Gate fail still emits JSON on stdout when --output is omitted.
    try:
        cli_json = json.loads(cli.stdout)
    except json.JSONDecodeError:
        print(f"FAIL: CLI did not emit JSON (exit {cli.returncode}): {cli.stderr}")
        return 1

    with tempfile.TemporaryDirectory(prefix="netdiff-wasm-") as tmp:
        script = Path(tmp) / "run.js"
        script.write_text(NODE_SCRIPT, encoding="utf-8")
        node = subprocess.run(
            ["node", str(script), str(args.wasm_js.resolve()),
             str(args.before.resolve()), str(args.after.resolve())],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            cwd=str(args.wasm_js.parent),
        )
    if node.returncode != 0:
        print(f"FAIL: node wasm run failed: {node.stderr}")
        return 1
    try:
        wasm_json = json.loads(node.stdout)
    except json.JSONDecodeError:
        print(f"FAIL: WASM output not JSON: {node.stdout[:200]}")
        return 1
    if "error" in wasm_json:
        print(f"FAIL: WASM error {wasm_json['error']}")
        return 1

    cli_gate = (cli_json.get("summary") or {}).get("gate")
    wasm_gate = (wasm_json.get("summary") or {}).get("gate")
    cli_sig = (cli_json.get("summary") or {}).get("significant_count")
    wasm_sig = (wasm_json.get("summary") or {}).get("significant_count")
    if cli_gate != wasm_gate or cli_sig != wasm_sig:
        print(f"FAIL: mismatch CLI(gate={cli_gate},sig={cli_sig}) "
              f"WASM(gate={wasm_gate},sig={wasm_sig})")
        return 1
    print(f"PASS: WASM matches CLI (gate={cli_gate}, significant={cli_sig})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
