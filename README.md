# NetDiff — Semantic Version Control & CI for Circuit Design

> **Working codename:** NetDiff. Rename freely — the name appears as `NetDiff` / `netdiff`
> throughout; do a global find-and-replace to change it.

**One line:** NetDiff diffs the *electrical connectivity* of KiCad schematics — not the picture —
so teams can catch the commit that silently changed the circuit, and gate it in CI.

Existing visual-diff tools render two schematics to images and highlight moved pixels. That is
noisy (a wire nudged 2 mm lights up) and unsafe (a deleted pull-up or a rerouted net can hide in
a visually similar image). NetDiff instead extracts the netlist/connectivity graph from each
revision and reports **what changed about the circuit**: nets added/removed/renamed/merged/split,
pins moved from one net to another, components added/removed/re-valued. Then it classifies each
change as electrically significant or cosmetic, and returns a non-zero exit code in CI when the
topology changed.

---

## Why this document set exists

This is the complete build specification for NetDiff, written to be executed by an AI coding agent
(Cursor) **under human supervision, one phase at a time**. It is not an autopilot. Read
`docs/05_BUILD_PLAN.md` and drive the agent phase-by-phase, reviewing and testing each phase's
output against real schematics before starting the next. The single most common failure mode is
letting the agent race ahead and produce code that compiles and demos but is wrong on multi-sheet
designs. Do not do that.

## How to use this with Cursor

1. Create the repository and drop this entire `docs/` folder plus `AGENTS.md` at the repo root.
2. Copy the existing C++ engine (the KiCad `.kicad_sch` parser + connectivity resolver you already
   built) into `engine/` as described in `docs/01_ARCHITECTURE.md`.
3. Open Cursor. Point it at `AGENTS.md` first (it sets the rules of engagement).
4. Work through `docs/05_BUILD_PLAN.md` in order. For each task, paste the task block into Cursor,
   let it implement, then run the acceptance check yourself before accepting.
5. Never merge a phase whose acceptance criteria (in the build plan) are unmet.

## Document index

| File | Purpose |
|------|---------|
| `docs/00_PRD.md` | Product requirements: vision, users, problem, scope, features, metrics, non-goals |
| `docs/01_ARCHITECTURE.md` | System architecture, tech stack, repo layout, build system |
| `docs/02_DATA_MODEL.md` | Canonical schematic model, connectivity graph, and diff data structures (schemas) |
| `docs/03_DIFF_ALGORITHM.md` | The semantic diff algorithm — the core IP. Matching, classification, noise suppression |
| `docs/04_CLI_CI_SPEC.md` | CLI surface, output formats, exit codes, CI integrations, KiCad plugin |
| `docs/05_BUILD_PLAN.md` | Phased, task-level plan the agent executes against, with acceptance criteria |
| `docs/06_TESTING_QA.md` | Test strategy, golden corpus, the KiCad-netlist correctness oracle |
| `docs/07_GTM_MONETIZATION.md` | Positioning, pricing, distribution, the free→paid boundary |
| `AGENTS.md` | Coding standards and rules of engagement for the AI agent |

## Ground truth you must not skip

- **Correctness oracle:** NetDiff's extracted netlist must match KiCad's own exported netlist
  (`kicad-cli sch export netlist`) on a corpus of real open-source designs. This is both your test
  gate and your marketing proof. See `docs/06_TESTING_QA.md`.
- **Multi-sheet is mandatory.** Real team designs are hierarchical/multi-sheet. A tool that only
  handles flat single-sheet schematics is a toy. This is called out as a hard requirement in the PRD.
- **Legal:** the existing engine is a fork. Resolve ownership/licensing before any commercial
  release. See the LICENSING note in `docs/00_PRD.md` §9.
