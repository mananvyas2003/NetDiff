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

## Install (no toolchain)

CI publishes platform archives on GitHub Releases (`v*` tags via `.github/workflows/release.yml`):

| Asset | Platform |
|-------|----------|
| `netdiff-linux-x86_64.tar.gz` | Linux x86_64 |
| `netdiff-macos-arm64.tar.gz` | macOS Apple Silicon |
| `netdiff-macos-x86_64.tar.gz` | macOS Intel |
| `netdiff-windows-x86_64.zip` | Windows x86_64 |

Each archive contains the `netdiff` binary plus `NOTICE.txt`. Matching `*.sha256` files are attached.

**One-liner (Unix)** — set your GitHub `owner/repo`, then:

```bash
export NETDIFF_REPO=OWNER/REPO   # e.g. your-org/netdiff
curl -fsSL "https://raw.githubusercontent.com/${NETDIFF_REPO}/master/scripts/install.sh" | bash
netdiff version
```

**Manual download**

```bash
# Linux x86_64 example
curl -fsSL -o netdiff.tgz \
  "https://github.com/OWNER/REPO/releases/latest/download/netdiff-linux-x86_64.tar.gz"
tar -xzf netdiff.tgz
sudo mv netdiff /usr/local/bin/
netdiff version
```

```powershell
# Windows (PowerShell) example
$verUrl = "https://github.com/OWNER/REPO/releases/latest/download/netdiff-windows-x86_64.zip"
Invoke-WebRequest $verUrl -OutFile netdiff.zip
Expand-Archive netdiff.zip -DestinationPath .
.\netdiff.exe version
```

Local packaging from a build tree (what CI runs):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target netdiff_cli
python scripts/package_binary.py --binary build/cli/netdiff --out dist
```

## GitHub Action

On pull requests, use the composite action in `ci/` (`docs/04_CLI_CI_SPEC.md` §2.1):

```yaml
- uses: actions/checkout@v4
  with:
    fetch-depth: 0
- uses: OWNER/REPO/ci@v0.1.0
  with:
    path: .
    comment: true
    sarif: true
```

See `ci/README.md` for inputs, permissions, and dogfooding with a locally built binary.

## GitLab CI & pre-commit

- **GitLab:** include `ci/gitlab-netdiff.yml` (set `NETDIFF_REPO` to the GitHub project that
  publishes release binaries). See comments at the top of that file.
- **pre-commit:** add this repo as a hook source (`ci/.pre-commit-hooks.yaml`, id `netdiff`) or
  run `python ci/pre_commit_netdiff.py` locally. Blocks commits when `netdiff diff --staged`
  gate-fails; bypass with `git commit --no-verify`.

## KiCad plugin

See `plugin/README.md`. Install `plugin/plugins/netdiff_kicad/` into KiCad's scripting plugins
folder, ensure the `netdiff` binary is on `PATH` / `NETDIFF_BIN`, then use
**NetDiff: review my changes**.

## Browser demo (WASM)

- **Dashboard (recommended):** `web/dashboard/` — workspace, filters, inspector, local history, export.
- **Minimal demo:** `web/demo/` — drop two files, print a short summary.

Build the WASM module with Emscripten (`emcmake cmake -S . -B build-wasm && cmake --build build-wasm --target netdiff_wasm`),
then:

```bash
python -m http.server 8080
# http://localhost:8080/web/dashboard/
```

CI workflow `.github/workflows/wasm.yml` produces the same artifacts for static/Vercel hosting.
Details: `web/dashboard/README.md`, `web/demo/README.md`.

## Using the CLI

Build from source (CMake + any C++17 compiler); the product binary lands at `build/cli/netdiff`:

```
cmake -S . -B build && cmake --build build -j 8
ctest --test-dir build --output-on-failure
```

```
netdiff diff <before> <after>            # two project dirs or entry .kicad_sch files
netdiff diff --git <ref_a> <ref_b> [path]  # two git revisions of the project
netdiff diff --staged [path]             # working tree vs HEAD
netdiff graph <schematic>                # the ConnectivityGraph as JSON
netdiff validate [path]                  # does this project parse and resolve?
netdiff version
```

Options: `--format text|json|markdown|sarif|html`, `--output <file>`, `--config <path>`,
`--fail-on significant|any|never`, `--no-color`, `--quiet`, `--include-cosmetic`.

Exit codes are a CI contract: `0` gate passed, `1` gate failed (connectivity changed),
`2` usage error, `3` parse/resolve error, `4` internal error. A parse error never exits 0.

Configuration is read from `--config`, else the nearest `.netdiff.yml` walking up to the
repository root, else built-in defaults; CLI flags override the file. See
`docs/02_DATA_MODEL.md` §4 for the schema.

### Optional: register as a git difftool

Not required — the CLI works standalone — but you can wire it into `git difftool`:

```
git config --local diff.netdiff.command \
  'sh -c "netdiff diff \"$LOCAL\" \"$REMOTE\" --no-color"'
echo '*.kicad_sch diff=netdiff' >> .gitattributes
```

For reviewing a whole change rather than one file, prefer `netdiff diff --git <base> <head>`:
it fetches *every* sheet of a hierarchical project at each revision, which per-file difftools
cannot do.

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
