#!/usr/bin/env python3
"""KiCad Action plugin entry — shells out to netdiff only."""

from __future__ import annotations

import os
import traceback
from pathlib import Path


def _project_path_from_kicad() -> str | None:
    """Best-effort: current schematic / board project directory."""
    try:
        import pcbnew  # type: ignore

        board = pcbnew.GetBoard()
        if board is not None:
            path = board.GetFileName()
            if path:
                return str(Path(path).resolve().parent)
    except Exception:
        pass

    env = os.environ.get("KIPRJMOD") or os.environ.get("KICAD_PROJECT_DIR")
    if env:
        return env
    return None


def _is_git_repo(path: str) -> bool:
    p = Path(path).resolve()
    if p.is_file():
        p = p.parent
    for parent in [p, *p.parents]:
        if (parent / ".git").exists():
            return True
    return False


def _navigate(row) -> None:
    """Try to focus a symbol; fall back to a message with ref + position."""
    import wx

    detail = row.message
    if row.ref:
        detail = f"{row.ref}: {row.message}"
    if row.x is not None and row.y is not None:
        detail += f"\nposition: ({row.x:.3f}, {row.y:.3f})"
    if row.sheet_path:
        detail += f"\nsheet: {row.sheet_path}"

    # Schematic Python navigation APIs vary by KiCad version; keep offline and
    # degrade gracefully with copy-friendly details.
    try:
        if wx.TheClipboard.Open():
            wx.TheClipboard.SetData(wx.TextDataObject(row.ref or row.message))
            wx.TheClipboard.Close()
            detail += "\n\n(Reference copied to clipboard where available.)"
    except Exception:
        pass

    wx.MessageBox(detail, "NetDiff — navigate", wx.OK | wx.ICON_INFORMATION)


def run_review(parent=None) -> None:
    import wx

    from .binary import require_netdiff_binary
    from .dialog import pick_two_files, show_results_dialog
    from .runner import NetDiffRunError, run_diff_json

    if parent is None:
        parent = wx.GetApp().GetTopWindow() if wx.GetApp() else None

    try:
        binary = require_netdiff_binary(Path(__file__).resolve().parent)
    except FileNotFoundError as exc:
        wx.MessageBox(str(exc), "NetDiff", wx.OK | wx.ICON_ERROR)
        return

    project = _project_path_from_kicad() or os.getcwd()
    try:
        if _is_git_repo(project):
            diff, _rc = run_diff_json(binary, staged=True, project=project)
        else:
            wx.MessageBox(
                "This project is not a git repository.\n"
                "Choose a before/after .kicad_sch pair instead.",
                "NetDiff",
                wx.OK | wx.ICON_INFORMATION,
            )
            picked = pick_two_files(parent)
            if not picked:
                return
            before, after = picked
            diff, _rc = run_diff_json(
                binary, staged=False, before=before, after=after, project=project
            )
    except NetDiffRunError as exc:
        wx.MessageBox(str(exc), "NetDiff", wx.OK | wx.ICON_ERROR)
        return
    except Exception:
        wx.MessageBox(traceback.format_exc(), "NetDiff", wx.OK | wx.ICON_ERROR)
        return

    show_results_dialog(parent, diff, on_navigate=_navigate)


class NetDiffActionPlugin:
    """pcbnew.ActionPlugin-compatible facade (registered when pcbnew exists)."""

    def defaults(self) -> None:
        self.name = "NetDiff: review my changes"
        self.category = "Diff"
        self.description = (
            "Semantic connectivity diff of the open project (shells out to netdiff)."
        )
        self.show_toolbar_button = True
        self.icon_file_name = ""

    def Run(self) -> None:
        run_review()


def register() -> None:
    try:
        import pcbnew  # type: ignore

        class _Plugin(pcbnew.ActionPlugin, NetDiffActionPlugin):
            def defaults(self) -> None:  # noqa: D401
                NetDiffActionPlugin.defaults(self)

            def Run(self) -> None:
                NetDiffActionPlugin.Run(self)

        _Plugin().register()
    except Exception:
        # Not inside KiCad — CLI / unit-test import still works.
        pass
