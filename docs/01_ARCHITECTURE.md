# 01 — Architecture & Tech Stack

## 1. System overview

```
                     ┌──────────────────────────────────────────────┐
                     │                 libnetdiff (C++17)            │
  .kicad_sch  ─────► │  lexer → parser → interpreter → net_resolver  │ ─┐
  (+ sub-sheets)     │             → connectivity graph              │  │  ConnectivityGraph
                     └──────────────────────────────────────────────┘  │  (canonical netlist)
                                                                        ▼
                     ┌──────────────────────────────────────────────┐
   graph A, graph B  │              diff core (C++17)                │ ─►  DiffResult
                     │  match components → match nets → classify →   │     (structured)
                     │  suppress cosmetic noise                       │
                     └──────────────────────────────────────────────┘
                                                                        │
        ┌───────────────────────────┬───────────────────────────┬─────┴───────────────┐
        ▼                           ▼                           ▼                     ▼
   netdiff CLI                 KiCad plugin              WASM module            Hosted service
 (text/json/md/sarif,        (Python, in-editor)     (browser demo, web UI)   (PR bot, dashboards,
  git integration, CI)                                                          auth, billing)
```

The C++ core is the single source of truth for parsing + connectivity + diff. Every surface (CLI,
plugin, browser, server) calls the *same* core so results are identical everywhere. Do not
reimplement connectivity logic in any other language.

## 2. Components

### 2.1 `libnetdiff` (C++17) — connectivity engine
Reuse the existing engine. Refactor it from a `main()`-driven program into a library with a clean
API. It must:
- Take a project entry `.kicad_sch` and follow hierarchical sheet references.
- Produce a `ConnectivityGraph` (schema in `02_DATA_MODEL.md`).
- Expose: `ConnectivityGraph BuildGraph(const ProjectInput&)`.
- Contain **no** I/O side effects, no printing, no server, no platform-specific calls on the core
  path. (The current Windows-only web dashboard `web_dashboard.cpp` is removed from the core; any UI
  lives in a surface, not the engine.)

### 2.2 diff core (C++17) — part of `libnetdiff`
- `DiffResult Diff(const ConnectivityGraph& before, const ConnectivityGraph& after, const DiffConfig&)`.
- Pure function. Deterministic. Fully specified in `03_DIFF_ALGORITHM.md`.

### 2.3 `netdiff` CLI (C++)
- Thin front-end over the library. Argument parsing, Git plumbing, output formatting.
- Formats: text, json, markdown, sarif. Exit codes per `04_CLI_CI_SPEC.md`.
- Statically linked where possible for zero-dependency distribution.

### 2.4 KiCad plugin (Python)
- Uses KiCad's Python API / Action plugin mechanism; shells out to the `netdiff` binary (do **not**
  reimplement diff in Python).
- Distributed via KiCad's Plugin & Content Manager.

### 2.5 WASM module (Emscripten)
- Compile `libnetdiff` to WebAssembly for the browser: powers the marketing demo and the hosted web
  UI's client-side diffing (so IP never leaves the browser for the demo tier).

### 2.6 Hosted service (Phase 3)
- Frontend: **Next.js** on Vercel (you already have a Vercel deployment — reuse it).
- Backend: **Node.js + TypeScript** (or Go) service that runs the CLI/WASM, plus a **GitHub App**
  for PR comments/checks.
- DB: **PostgreSQL**. Auth: an off-the-shelf provider. Billing: Stripe.
- On-prem/self-hosted distribution: ship the same backend as a Docker image for IP-sensitive
  customers (this is a paid tier, and a key differentiator).

## 3. Tech stack (authoritative)

| Layer | Choice | Rationale |
|-------|--------|-----------|
| Core engine + diff | C++17 | Reuse existing engine; fast; compiles to native + WASM |
| Build system | **CMake** (replace the Visual Studio `.sln`) | Cross-platform, CI-friendly, WASM-capable |
| Core deps | Standard library only where possible; a single-header JSON lib (e.g. nlohmann/json) for serialization | Minimize footprint |
| HTTP (surfaces only, not core) | cpp-httplib (single header) *only if* a local server is needed | Remove Windows-only Winsock code |
| CLI arg parsing | CLI11 or a hand-rolled parser | Small, header-only |
| Tests | GoogleTest (unit) + a Python/bash harness for the netlist oracle | Standard, CI-friendly |
| WASM | Emscripten | Browser + web UI |
| CLI packaging | GitHub Actions matrix → static binaries for linux/macos/windows | Zero-dependency installs |
| Plugin | Python (KiCad API) | Required by KiCad |
| Web frontend | Next.js (Vercel) | Reuse existing deployment |
| Backend | Node.js + TypeScript (or Go) | Fast to build; GitHub App ecosystem |
| DB | PostgreSQL | Standard |
| Billing | Stripe | Standard |
| Container | Docker (self-hosted/on-prem tier) | IP-sensitive customers |

If the agent proposes swapping any authoritative choice, it must flag it explicitly and wait for
human sign-off (see `AGENTS.md`).

## 4. Repository layout

```
netdiff/
├── AGENTS.md
├── README.md
├── LICENSE                       # resolve licensing before commercial release (PRD §9)
├── CMakeLists.txt
├── docs/                         # this document set
├── engine/                       # libnetdiff: existing C++ engine, refactored
│   ├── include/netdiff/          # public headers (clean API)
│   ├── src/                      # lexer, parser, interpreter, net_resolver, graph, diff
│   └── CMakeLists.txt
├── cli/                          # netdiff CLI
│   └── src/
├── bindings/
│   └── wasm/                     # Emscripten build
├── plugin/                       # KiCad Python plugin
├── ci/                           # GitHub Action, GitLab template, pre-commit config
├── tests/
│   ├── unit/                     # GoogleTest
│   ├── corpus/                   # golden open-source KiCad projects + expected netlists
│   └── oracle/                   # harness comparing our netlist to kicad-cli's
├── web/                          # Phase 3: Next.js frontend
└── server/                      # Phase 3: backend + GitHub App
```

## 5. Data flow contracts (must not drift)

1. **Parse contract:** parser output is an AST; interpreter output is a `Schematic` model; resolver
   output is a `ConnectivityGraph`. Each stage has a typed boundary. No stage prints.
2. **Serialization contract:** `ConnectivityGraph` serializes to a stable, versioned JSON
   (`schema_version` field). Two runs on the same input produce byte-identical JSON.
3. **Diff contract:** `Diff(before, after, config) → DiffResult`. Pure, deterministic, order-stable.
4. **Surface contract:** CLI/plugin/WASM/server never contain connectivity logic — they only call
   the library and format results.

## 6. Cross-platform requirements

- One `CMakeLists.txt` builds on Linux, macOS, Windows. CI builds all three every push.
- No `<windows.h>`, `ShellExecute`, or platform `#ifdef` on the core path. If a surface needs to
  open a browser, that lives in the surface, guarded per-platform, never in `libnetdiff`.
- Fix the known portability bug: the engine currently relies on a transitive `<cmath>` include that
  only works on MSVC. Add explicit includes; compile with `-Wall -Wextra` and treat warnings as
  errors in CI.
