#!/usr/bin/env python3
"""wx dialog listing significant NetDiff changes."""

from __future__ import annotations

from typing import Callable

from .model import ChangeRow, group_by_type, significant_rows


def show_results_dialog(
    parent,
    diff: dict,
    *,
    on_navigate: Callable[[ChangeRow], None] | None = None,
) -> None:
    import wx

    rows = significant_rows(diff)
    summary = diff.get("summary") or {}
    gate = summary.get("gate", "?")
    sig = summary.get("significant_count", len(rows))

    frame = parent
    dlg = wx.Dialog(
        frame,
        title=f"NetDiff — {gate} ({sig} significant)",
        size=wx.Size(720, 480),
        style=wx.DEFAULT_DIALOG_STYLE | wx.RESIZE_BORDER,
    )
    root = wx.BoxSizer(wx.VERTICAL)
    hint = wx.StaticText(
        dlg,
        label="Significant electrical changes. Double-click a row to navigate (when supported).",
    )
    root.Add(hint, 0, wx.ALL | wx.EXPAND, 8)

    tree = wx.TreeCtrl(dlg, style=wx.TR_DEFAULT_STYLE | wx.TR_HIDE_ROOT | wx.TR_FULL_ROW_HIGHLIGHT)
    root_id = tree.AddRoot("changes")
    row_by_item: dict = {}
    for change_type, items in group_by_type(rows).items():
        branch = tree.AppendItem(root_id, f"{change_type} ({len(items)})")
        for row in items:
            label = row.message
            if row.critical:
                label = "⚠ " + label
            item = tree.AppendItem(branch, label)
            row_by_item[item] = row
        tree.Expand(branch)

    if not rows:
        tree.AppendItem(root_id, "No electrical changes.")

    root.Add(tree, 1, wx.ALL | wx.EXPAND, 8)

    def on_activate(event):
        item = event.GetItem()
        row = row_by_item.get(item)
        if row and on_navigate:
            on_navigate(row)

    tree.Bind(wx.EVT_TREE_ITEM_ACTIVATED, on_activate)

    buttons = dlg.CreateButtonSizer(wx.CLOSE)
    if buttons:
        root.Add(buttons, 0, wx.ALL | wx.ALIGN_RIGHT, 8)
    dlg.SetSizer(root)
    dlg.ShowModal()
    dlg.Destroy()


def pick_two_files(parent) -> tuple[str, str] | None:
    import wx

    with wx.FileDialog(
        parent,
        "Before schematic / project",
        wildcard="KiCad schematic (*.kicad_sch)|*.kicad_sch|All|*.*",
        style=wx.FD_OPEN | wx.FD_FILE_MUST_EXIST,
    ) as before_dlg:
        if before_dlg.ShowModal() != wx.ID_OK:
            return None
        before = before_dlg.GetPath()

    with wx.FileDialog(
        parent,
        "After schematic / project",
        wildcard="KiCad schematic (*.kicad_sch)|*.kicad_sch|All|*.*",
        style=wx.FD_OPEN | wx.FD_FILE_MUST_EXIST,
    ) as after_dlg:
        if after_dlg.ShowModal() != wx.ID_OK:
            return None
        after = after_dlg.GetPath()
    return before, after
