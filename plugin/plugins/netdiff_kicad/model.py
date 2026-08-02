#!/usr/bin/env python3
"""Parse DiffResult JSON into UI rows — no connectivity logic."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any


@dataclass(frozen=True)
class ChangeRow:
    change_type: str
    message: str
    critical: bool
    ref: str | None
    sheet_path: str | None
    x: float | None
    y: float | None


def _component_nav(component: dict[str, Any] | None) -> tuple[str | None, str | None, float | None, float | None]:
    if not component:
        return None, None, None, None
    pos = component.get("position") or {}
    return (
        component.get("ref"),
        component.get("sheet_path"),
        pos.get("x"),
        pos.get("y"),
    )


def _nav_from_payload(change_type: str, payload: dict[str, Any]) -> tuple[str | None, str | None, float | None, float | None]:
    if change_type in {"ComponentAdded", "ComponentRemoved"}:
        return _component_nav(payload.get("component"))
    if change_type == "ComponentModified":
        return payload.get("ref"), None, None, None
    if change_type == "PinConnectionChanged":
        pin_id = payload.get("pin_id") or ""
        ref = pin_id.split(".", 1)[0] if pin_id else None
        return ref, None, None, None
    if change_type in {"NetAdded", "NetRemoved"}:
        net = payload.get("net") or {}
        pins = net.get("pins") or []
        ref = pins[0].split(".", 1)[0] if pins else None
        return ref, None, None, None
    if change_type == "NetRenamed":
        net = payload.get("net") or {}
        pins = net.get("pins") or []
        ref = pins[0].split(".", 1)[0] if pins else None
        return ref, None, None, None
    if change_type in {"NetMerged", "NetSplit"}:
        return None, None, None, None
    return None, None, None, None


def significant_rows(diff: dict[str, Any]) -> list[ChangeRow]:
    rows: list[ChangeRow] = []
    for change in diff.get("changes") or []:
        if change.get("significance") != "SIGNIFICANT":
            continue
        change_type = str(change.get("type") or "Unknown")
        payload = change.get("payload") or {}
        ref, sheet, x, y = _nav_from_payload(change_type, payload)
        rows.append(
            ChangeRow(
                change_type=change_type,
                message=str(change.get("message") or change_type),
                critical=bool(change.get("critical")),
                ref=ref,
                sheet_path=sheet,
                x=x if isinstance(x, (int, float)) else None,
                y=y if isinstance(y, (int, float)) else None,
            )
        )
    rows.sort(key=lambda r: (r.change_type, r.message))
    return rows


def group_by_type(rows: list[ChangeRow]) -> dict[str, list[ChangeRow]]:
    grouped: dict[str, list[ChangeRow]] = {}
    for row in rows:
        grouped.setdefault(row.change_type, []).append(row)
    return grouped
