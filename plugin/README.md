# NetDiff KiCad plugin (T2.4)

Action plugin: **NetDiff: review my changes.**

- Shells out to the `netdiff` binary (`docs/04_CLI_CI_SPEC.md` §3) — no connectivity logic in Python.
- Offline only.
- Prefer `netdiff diff --staged` when the project is a git repo; otherwise offer a before/after file picker.
- Shows significant changes grouped by type; double-click copies navigation details (ref / position).

## Local install (development)

1. Build or install `netdiff` so it is on `PATH`, or set `NETDIFF_BIN`, or copy the binary to
   `plugin/plugins/netdiff_kicad/bin/netdiff` (`.exe` on Windows).
2. Copy or symlink this folder into your KiCad scripting plugins directory, e.g.

   - Windows: `%APPDATA%\kicad\8.0\scripting\plugins\netdiff_kicad_bundle\`
   - Linux: `~/.local/share/kicad/8.0/scripting/plugins/`
   - macOS: `~/Library/Preferences/kicad/8.0/scripting/plugins/`

   Ensure `plugins/__init__.py` is importable (the `netdiff_kicad` package must be on `PYTHONPATH`
   or sit next to the entry `__init__.py` as shipped here).

3. Restart KiCad. The action appears under Tools / plugins as **NetDiff: review my changes** when
   `pcbnew.ActionPlugin` registration succeeds (KiCad version dependent).

## Headless check (no KiCad)

```bash
python tests/plugin/test_plugin_model.py
NETDIFF_BIN=build/cli/netdiff python -c "from plugin.plugins.netdiff_kicad.binary import require_netdiff_binary; print(require_netdiff_binary())"
```
