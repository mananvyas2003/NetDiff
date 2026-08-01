# 00 — Product Requirements Document (PRD)

**Product:** NetDiff — Semantic Version Control & CI for Circuit Design
**Status:** v1 spec for build
**Owner:** (you)

---

## 1. Problem

Hardware teams keep schematics in Git, but Git and existing diff tools cannot tell them *what
changed about the circuit*. Two failure modes result:

1. **False alarms.** Visual diff tools render each revision to an image and highlight moved pixels.
   A wire nudged, a label repositioned, or a symbol rotated lights up as a "change" even though the
   circuit is electrically identical. Reviewers learn to ignore the diff.
2. **Silent breakage.** The dangerous changes — a deleted pull-up resistor, a net that got merged
   into GND, a pin re-tied to the wrong rail, a decoupling cap dropped — can be visually subtle and
   slip through review. The board comes back from fab broken, costing weeks and a respin.

The root cause: existing tools diff the **drawing**. Engineers care about the **connectivity**.
KiCad stores schematics as text (S-expressions), and it can export a netlist, but nothing in the
common workflow diffs the *netlist between two revisions* and presents it as reviewable, gating
signal in CI.

## 2. Solution

NetDiff extracts a canonical connectivity graph (netlist) from each schematic revision and computes
a **semantic diff** over connectivity:

- Nets added, removed, renamed, merged, or split.
- Pins that moved from one net to another.
- Components added, removed, or changed (value/footprint).
- Each change classified **electrically significant** vs **cosmetic**.

Delivered as:
- A cross-platform **CLI** that diffs two files or two Git revisions.
- **CI integrations** (GitHub Action, GitLab CI, pre-commit) that fail the build / comment on PRs
  when connectivity changed.
- A **KiCad plugin** for in-editor review of working-tree-vs-HEAD changes.
- (Later) a **hosted service** with a web review UI, a GitHub PR bot, and team dashboards — the
  monetization layer.

The connectivity engine already exists in C++ (the KiCad `.kicad_sch` parser + geometric net
resolver). NetDiff wraps and hardens it, then adds the diff core and the delivery surfaces.

## 3. Target users & buyers

- **Primary user:** electrical/hardware engineers on teams (2–50 engineers) using KiCad with Git.
- **Secondary user:** the reviewer/lead who approves schematic changes and does design review.
- **Buyer:** engineering lead / hardware team manager at a startup or mid-size hardware company.
- **Highest-value segment (differentiator):** IP-sensitive orgs (defense, medical, aerospace,
  industrial) that **cannot upload schematics to a cloud reviewer**. NetDiff runs locally/on-prem
  and in their own CI — this is the wedge cloud competitors structurally cannot serve. See §8.

Non-buyers to deprioritize: solo hobbyists (won't pay), pure enterprise EDA (Altium/Cadence) users
who don't use KiCad (v1 is KiCad-only; see roadmap).

## 4. Goals & non-goals

**Goals (v1)**
- Extract a connectivity graph from any KiCad `.kicad_sch` project, **including multi-sheet /
  hierarchical designs and buses**.
- Compute a correct, low-noise semantic connectivity diff between two revisions.
- Run in CI with a meaningful exit code and machine-readable output (JSON + SARIF).
- Match KiCad's own exported netlist on the test corpus (correctness oracle).
- Cross-platform: Linux, macOS, Windows.

**Non-goals (v1) — explicitly out of scope**
- No PCB generation, placement, or autorouting. NetDiff is not an EDA generator.
- No layout/`.kicad_pcb` diff in v1 (roadmap, not v1).
- No AI/LLM design *judgment* in v1. The diff is deterministic. (An AI "explain this change in plain
  English" layer is a Phase 3 add-on, clearly separated, and must never be on the correctness path.)
- No Altium/Eagle support in v1 (roadmap).
- No SPICE, no simulation, no ERC replacement. NetDiff complements ERC; it does not replace it.

## 5. Functional requirements

- **FR-1** Parse a `.kicad_sch` (and its referenced hierarchical sheets) into an AST. Reuse existing
  engine.
- **FR-2** Resolve connectivity into a canonical netlist: components with pins, nets as pin-sets.
  Handle wires, junctions, local labels, global labels, hierarchical labels, power symbols, buses,
  and no-connects.
- **FR-3** Produce a stable, serializable connectivity graph (schema in `02_DATA_MODEL.md`).
- **FR-4** Given two connectivity graphs, match components and nets across revisions and compute a
  structured diff (algorithm in `03_DIFF_ALGORITHM.md`).
- **FR-5** Classify each change as electrically significant or cosmetic; suppress pure-graphical
  (coordinate-only) noise.
- **FR-6** Emit diff in multiple formats: human-readable text, JSON, Markdown (for PR comments),
  and SARIF (for CI code-scanning surfaces).
- **FR-7** CLI: diff two files, or diff two Git revisions of a project, or working-tree-vs-HEAD.
- **FR-8** CI: GitHub Action + GitLab CI template + pre-commit hook; exit non-zero when
  electrically-significant changes exist (configurable severity gate).
- **FR-9** KiCad plugin: run diff for the current project's uncommitted changes and show a report.
- **FR-10** Configurable rules file (`.netdiff.yml`): severity thresholds, ignore lists, net-name
  normalization, which change classes gate CI.

## 6. Non-functional requirements

- **Performance:** diff a typical 50–150 component board in < 500 ms; a 1000-component multi-sheet
  design in < 5 s, on a laptop.
- **Correctness:** netlist extraction matches `kicad-cli` netlist on 100% of the golden corpus (see
  `06_TESTING_QA.md`). Any mismatch is a release blocker.
- **Determinism:** identical inputs always produce byte-identical output (stable ordering).
- **Portability:** builds and runs on Linux, macOS, Windows from one CMake project. No
  platform-locked code on the core path (the current Windows-only web server must be removed/replaced).
- **Privacy:** the CLI and plugin run fully offline. No schematic data leaves the machine unless the
  user explicitly opts into the hosted service.
- **Stability:** malformed input never crashes; it returns a clear error and a non-zero exit code.

## 7. Success metrics

- **North star:** number of repositories with NetDiff running in CI weekly.
- Activation: a team runs its first PR-gating diff within 24h of install.
- Correctness: 0 netlist mismatches vs oracle on the corpus.
- Noise: on a curated set of "cosmetic-only" commits, NetDiff reports **no** electrically-significant
  changes (false-positive rate target: < 2%).
- Business (post Phase 3): free→paid conversion of teams; paid seats; logo count in the IP-sensitive
  segment.

## 8. Differentiation & competitive position

- **Visual-diff tools (KiRi, kdiff, plotkicadsch, typecad-gitdiff, kicad-diff-visualizer):** diff the
  rendered image. NetDiff diffs connectivity — semantic, low-noise, CI-gating. Different category.
- **CADLAB.io:** commercial Git + visual diff + review for KiCad; cloud/browser. NetDiff differs on
  (a) semantic connectivity diff and (b) local/offline + self-hosted CI for IP-sensitive teams.
- **AI schematic reviewers (Traceformer, NextPCB Design Validator, galvano.ai):** cloud upload,
  judgment-based review, need EE-domain trust. Different problem (is-this-design-good vs
  what-changed). NetDiff's diff is objective and deterministic; it can *feed* a review layer later
  but does not compete on judgment in v1.
- **The moat:** a correct, fast, offline semantic connectivity engine + the CI/team workflow around
  it. Competitors doing pixel diff cannot cheaply replicate connectivity-level diff; cloud reviewers
  cannot serve teams that refuse to upload IP.

## 9. Constraints, risks, open questions

- **LICENSING (blocker):** the connectivity engine is a fork of another author's repo and currently
  has no LICENSE. Before any commercial release: (a) settle ownership/contribution in writing with
  the original author, (b) choose a license model (dual-license: source-available core + commercial
  hosted tier is a common fit). Do not ship commercially until resolved.
- **Risk — "feature not a company":** semantic diff alone may be a strong OSS project + modest paid
  plugin. It becomes a company only as the wedge into a team CI/review platform (Phase 3). Treat diff
  as the wedge, not the destination.
- **Risk — domain credibility:** the founder is a software/graph engineer, not (yet) a hardware
  engineer. Mitigation: recruit 3–5 hardware-team design partners early; get a hardware co-founder or
  advisor before selling the judgment/review layer.
- **Open question:** does the existing engine handle hierarchical/multi-sheet and buses today? If
  not, that is the first hardening task (Phase 0/1). This gates the whole product.
- **Open question:** exact matching heuristics for auto-named/unnamed nets across revisions — spec'd
  in `03_DIFF_ALGORITHM.md`, but needs tuning against real data.

## 10. Release phases (summary; detail in 05_BUILD_PLAN.md)

- **Phase 0 — Hardening & portability:** CMake, cross-platform, remove Windows-only code, extract a
  clean `libnetdiff` core from `main()`, netlist oracle test harness.
- **Phase 1 — Diff core + CLI:** semantic diff, classification, output formats, `netdiff` CLI, Git
  integration. **This is the MVP.**
- **Phase 2 — Distribution:** GitHub Action + GitLab CI + pre-commit, KiCad plugin, WASM build for
  browser demo, packaged binaries.
- **Phase 3 — Hosted platform (monetization):** web review UI, GitHub App PR bot, team dashboards,
  auth/billing, optional AI "explain this change" layer.
