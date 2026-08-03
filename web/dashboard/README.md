# NetDiff dashboard

Interactive review UI for connectivity diffs. All parsing/diffing runs **in the browser** via the
same `libnetdiff` WASM build as `web/demo/` — schematics are not uploaded.

## Features

- **Workspace** — before/after `.kicad_sch`, run diff, gate banner, KPI strip
- **Change browser** — filter significant / cosmetic / all, search, grouped list, payload inspector
- **History** — last 20 runs in `localStorage` (DiffResult JSON only)
- **Export** — Markdown (PR-friendly) and JSON
- **Sample result** — UI walkthrough without files (embedded fallback if golden JSON isn’t served)

## Run locally

From the repo root (so sample JSON under `tests/` can load):

```bash
python -m http.server 8080
# open http://localhost:8080/web/dashboard/
```

Or from `web/`:

```bash
python -m http.server 8080 --directory web
# open http://localhost:8080/dashboard/
```

Requires `web/demo/wasm/netdiff.js` + `netdiff.wasm` (built by `emcmake` / CI `wasm.yml`).

## Relation to Phase 3

This is the client-side review surface. Auth, GitHub App PR bot, teams, and billing remain Phase 3
(`docs/05_BUILD_PLAN.md`). The dashboard already uses the architecture’s WASM path so IP can stay
on-device for the free/demo tier.
