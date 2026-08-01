# 05 — Build Plan (Agent-Executable)

Drive Cursor through this in order. Each **task** is scoped to be implemented and reviewed as a unit.
For every task: implement → run the acceptance check → only then accept and continue. Do not batch
phases. Do not skip acceptance checks. "It compiles" is never an acceptance criterion by itself.

Legend: **[AC]** = acceptance criteria (must all pass before the task is done).

---

## Phase 0 — Hardening & portability (foundation)

**Goal:** turn the existing `main()`-driven engine into a clean, cross-platform library with a test
harness proving its netlist is correct. No new product features yet. This phase de-risks everything.

### T0.1 — CMake build, cross-platform
- Replace the Visual Studio `.sln`/`.vcxproj` with a root `CMakeLists.txt` + `engine/CMakeLists.txt`.
- Compile with `-std=c++17 -Wall -Wextra -Werror` (and MSVC equivalents).
- **[AC]** Builds cleanly on Linux, macOS, Windows in CI (add a GitHub Actions matrix). No warnings.

### T0.2 — Fix known portability defects
- Add explicit `#include <cmath>` and any other includes currently working only by transitive luck.
- Remove `web_dashboard.cpp` (Windows-only `<windows.h>` / `ShellExecuteA`) from the core build. Any
  UI moves to a surface later; the engine has no server and no platform calls.
- **[AC]** Grep shows no `<windows.h>`, `ShellExecute`, or `system(` on the core path. Core builds on
  all three OSes.

### T0.3 — Extract `libnetdiff` API from `main()`
- Move pipeline logic out of `main()` into a library. Public header `include/netdiff/graph.hpp`
  exposes `ConnectivityGraph BuildGraph(const ProjectInput&)`. No printing inside the library.
- Implement `ConnectivityGraph` per `02_DATA_MODEL.md`, with stable JSON serialization
  (`schema_version`, sorted keys/arrays, byte-stable).
- **[AC]** A tiny test program builds a graph from the sample ESP32 schematic and serializes it; two
  runs produce byte-identical JSON.

### T0.4 — Multi-sheet / hierarchical support (CRITICAL — gates the product)
- Follow hierarchical sheet references from the entry `.kicad_sch`; resolve connectivity across
  sheets (hierarchical labels, global labels, power symbols). Handle buses and no-connects.
- **[AC]** On a multi-sheet open-source design, the resolved net for a signal that crosses sheets is
  a single net spanning both sheets (verified against `kicad-cli` netlist).

### T0.5 — Netlist correctness oracle harness
- Build `tests/oracle/`: for each corpus project, run `kicad-cli sch export netlist`, parse it, and
  compare against `netdiff graph` output (component set, and each net's pin-set).
- **[AC]** Harness runs in CI and reports per-project pass/fail. See `06_TESTING_QA.md` for the
  corpus. Target: 100% pin-set match on the corpus. Any mismatch is a blocker.

### T0.6 — Robustness
- Malformed inputs (empty, garbage, unbalanced parens, deeply nested) never crash; return exit 3 with
  a clear message.
- **[AC]** A fuzz/edge test suite passes; no segfaults; sanitizers (ASan/UBSan) clean on the corpus.

**Phase 0 exit gate:** cross-platform build green; 100% oracle match on corpus incl. ≥3 multi-sheet
designs; sanitizers clean. Do not start Phase 1 until this holds.

---

## Phase 1 — Diff core + CLI (THE MVP)

**Goal:** ship a CLI that computes a correct, low-noise semantic diff and gates CI. This is the
sellable minimum.

### T1.1 — Diff data structures
- Implement `DiffResult` and all `Change` types from `02_DATA_MODEL.md` §3, with JSON serialization.
- **[AC]** Unit tests construct and serialize each change type deterministically.

### T1.2 — Component matching (Algorithm §3)
- **[AC]** Tests cover added/removed/modified, refdes-reuse-with-different-lib_id guard.

### T1.3 — Net matching tiers 1–3 (Algorithm §4)
- Exact-name, same-pin-set-rename, and Jaccard fuzzy matching with deterministic tie-breaks.
- **[AC]** Tests: renamed net detected; unnamed net that gained a pin is matched not
  reported-as-remove+add; ties break deterministically.

### T1.4 — Pin-level changes + merge/split (Algorithm §5–6)
- **[AC]** Tests: a pin moved GND→+3V3 yields exactly one PinConnectionChanged; two rails shorted
  yields NetMerged flagged critical; a net broken into two yields NetSplit.

### T1.5 — Significance classification + gate (Algorithm §7–8)
- **[AC]** The invariant test: take a schematic, move symbols/wires without changing connectivity,
  diff → `significant_count == 0`. This is the flagship correctness property.

### T1.6 — Output formatters
- text, json, markdown, sarif per `04_CLI_CI_SPEC.md`.
- **[AC]** Golden-output tests for each format on a fixed diff. SARIF validates against the 2.1.0
  schema.

### T1.7 — `netdiff` CLI + exit codes + git integration
- Implement all commands/options/exit-codes in `04_CLI_CI_SPEC.md`, including `--git` (multi-file
  checkout per revision) and `--staged`.
- **[AC]** End-to-end test: a git repo with two commits diffs correctly via `--git`; exit codes match
  the contract; parse error → exit 3.

**Phase 1 exit gate:** MVP runs on real repos, correct + deterministic + low-noise, exit codes solid.
This is the point to put it in front of design partners.

---

## Phase 2 — Distribution

**Goal:** get it into users' hands where they already work.

### T2.1 — Packaged binaries
- CI matrix builds static `netdiff` binaries for linux/macos/windows; attach to GitHub Releases;
  provide install one-liners.
- **[AC]** A fresh machine can install and run `netdiff version` with no toolchain.

### T2.2 — GitHub Action
- Per `04_CLI_CI_SPEC.md` §2.1: PR comment (idempotent), SARIF upload, gate.
- **[AC]** On a test repo PR, a connectivity change posts one comment and fails the check; a cosmetic
  change posts "no electrical changes" and passes.

### T2.3 — GitLab CI template + pre-commit hook
- **[AC]** Both run the CLI and gate correctly in their environments.

### T2.4 — KiCad plugin
- Per `04_CLI_CI_SPEC.md` §3. Shells to the binary; offline; navigable results.
- **[AC]** Installs via a local plugin build; "review my changes" shows correct significant changes on
  a modified project.

### T2.5 — WASM build + browser demo
- Emscripten build of `libnetdiff`; a static page where a user drops two `.kicad_sch` files and sees
  the diff, computed entirely client-side (no upload).
- **[AC]** Demo runs on the existing Vercel deployment; diff matches the native CLI on the same input.

**Phase 2 exit gate:** installable, in CI, in KiCad, and a public client-side demo. Adoption can start.

---

## Phase 3 — Hosted platform (monetization)

**Goal:** the paid team layer. Only build after Phase 1/2 show real adoption and you have design
partners asking for it.

### T3.1 — Backend service + GitHub App
- Next.js frontend (reuse Vercel) + Node/TS backend + Postgres. A GitHub App that runs diffs on PRs
  server-side and manages checks/comments at org scale.
- **[AC]** Installing the App on a repo gates PRs with zero local setup.

### T3.2 — Web review UI
- Browse a PR's connectivity changes, per-net drill-down, approve/annotate. WASM or server-side diff.
- **[AC]** A reviewer can open a PR and see + comment on connectivity changes in the browser.

### T3.3 — Auth, teams, billing
- Accounts, teams, Stripe plans (free vs paid; see `07_GTM_MONETIZATION.md`).
- **[AC]** Free tier limits enforced; paid unlocks team features; upgrade/downgrade works.

### T3.4 — Self-hosted / on-prem image (IP-sensitive tier)
- Dockerized backend the customer runs inside their own network; no schematic leaves their infra.
- **[AC]** `docker compose up` yields a working private instance gating a self-hosted repo.

### T3.5 — (Optional) AI "explain this change" layer
- An LLM summarizes a `DiffResult` in plain English and flags likely-risky changes. Strictly
  additive: it consumes the deterministic diff, never sits on the correctness path, and is clearly
  labeled as advisory.
- **[AC]** With AI disabled, all core behavior is unchanged. AI output cites specific changes from the
  diff, invents nothing.

---

## How to drive Cursor per task (workflow)
1. Give Cursor `AGENTS.md` + the relevant `docs/*` sections for the task.
2. Paste the task (e.g. "Implement T1.4 …") and its **[AC]**.
3. Require it to write the tests first or alongside, then the implementation.
4. Run the acceptance check yourself. If it fails, feed the failure back; do not accept on the
   agent's say-so.
5. Commit per task with a message referencing the task id (e.g. `feat(diff): T1.4 pin-level changes`).
