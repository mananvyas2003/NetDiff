#!/usr/bin/env python3
"""Shell out to the netdiff binary (offline)."""

from __future__ import annotations

import json
import subprocess
import tempfile
from pathlib import Path
from typing import Any


class NetDiffRunError(RuntimeError):
    def __init__(self, message: str, exit_code: int = 4):
        super().__init__(message)
        self.exit_code = exit_code


def run_diff_json(
    binary: Path,
    *,
    staged: bool = True,
    before: str | None = None,
    after: str | None = None,
    project: str = ".",
) -> tuple[dict[str, Any], int]:
    """Return (DiffResult dict, exit_code). Gate fail (1) still returns JSON."""
    with tempfile.TemporaryDirectory(prefix="netdiff-plugin-") as tmp:
        out = Path(tmp) / "diff.json"
        cmd = [str(binary), "diff"]
        if staged:
            cmd += ["--staged", project]
        else:
            if not before or not after:
                raise NetDiffRunError("file-vs-file mode needs before and after paths", 2)
            cmd += [before, after]
        cmd += ["--format", "json", "--output", str(out), "--no-color"]

        proc = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        if proc.returncode in (2, 3, 4) and not out.is_file():
            detail = (proc.stderr or proc.stdout or "").strip()
            raise NetDiffRunError(detail or f"netdiff exited {proc.returncode}", proc.returncode)
        if not out.is_file():
            raise NetDiffRunError("netdiff produced no JSON output", proc.returncode)
        data = json.loads(out.read_text(encoding="utf-8"))
        return data, proc.returncode
