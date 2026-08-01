# 06 — Testing & QA

Correctness is the product. A diff tool that is subtly wrong is worse than none — it gives false
confidence. Testing is not optional polish here; it is the moat.

## 1. The correctness oracle (most important test)

KiCad can export a netlist from a schematic. That netlist is your ground truth.

```
For each project in the corpus:
  expected = parse( kicad-cli sch export netlist <project>.kicad_sch )
  actual   = netdiff graph <project>.kicad_sch  (ConnectivityGraph JSON)
  assert   component set(expected) == component set(actual)
  assert   for every net: pin-set(expected) == pin-set(actual)   # names may normalize; pin-sets must match
```

- Run this in CI on every push.
- **Release blocker:** any pin-set mismatch on any corpus project.
- This is also your marketing proof: "matches KiCad's own netlist on N open-source designs."

## 2. The golden corpus

Assemble `tests/corpus/` from real, license-compatible open-source KiCad projects. Requirements:
- ≥ 20 projects spanning small (single sheet, <20 parts) to large (multi-sheet, >500 parts).
- **≥ 3 hierarchical / multi-sheet designs** (mandatory — this is where naive parsers break).
- At least a few using buses, power symbols, global + hierarchical labels, multi-unit parts, and
  no-connect flags.
- Include KiCad's own bundled demo projects.
- Record the source repo + license for each in `tests/corpus/MANIFEST.md`. Only use
  license-compatible sources.

## 3. Diff-specific test suites

### 3.1 The invariant suite (flagship)
For each corpus project, generate a "cosmetically perturbed" variant (move symbols, reroute wires,
reposition labels — **without** changing connectivity). Then:
```
assert Diff(original, perturbed).summary.significant_count == 0
```
This proves NetDiff ignores drawing changes. It is the single most important behavioral guarantee and
the thing that beats visual-diff tools. Automate the perturbation where possible; hand-author a few.

### 3.2 The mutation suite (true positives)
Programmatically or by hand, apply known connectivity mutations to corpus schematics and assert the
exact expected change:
- Delete a pull-up resistor → ComponentRemoved (+ possibly PinConnectionChanged on its net).
- Move a pin's wire from GND to a signal → exactly one PinConnectionChanged with correct before/after.
- Short two rails → NetMerged, flagged critical.
- Split a net → NetSplit.
- Rename a labeled net → NetRenamed, significant.
- Change a resistor value → ComponentModified.
Each mutation is a fixture with an expected `DiffResult` (golden JSON).

### 3.3 Determinism suite
- Diff the same pair 100× → byte-identical output every time.
- Shuffle input file ordering / internal ordering → identical output (proves order-independence).

### 3.4 Edge-case suite (from Algorithm §11)
Empty schematic, single component, no nets, no-connects, buses, duplicate labels across sheets,
whole-sheet add/remove, unnamed-net-gained-a-pin, refdes reused with different lib_id.

## 4. Unit tests
- GoogleTest for: lexer, parser, interpreter, resolver, each matching tier, classification, gate,
  each output formatter.
- Coverage target: high on the diff core and resolver specifically (these carry the risk).

## 5. Fuzzing & memory safety
- Fuzz the parser (malformed S-expressions). No crashes; graceful exit 3.
- Build the corpus + suites under ASan and UBSan in CI; must be clean.

## 6. Output-format validation
- SARIF output validated against the SARIF 2.1.0 JSON schema.
- Markdown output snapshot-tested (it becomes PR comments; regressions are visible to users).
- JSON output validated against a published JSON Schema for `DiffResult`.

## 7. Performance tests
- Benchmark on the largest corpus board; enforce the budgets in `00_PRD.md` §6 as CI thresholds
  (fail if a change regresses diff time beyond the budget).

## 8. CI pipeline (assemble in `ci/`)
Every push runs, in order:
1. Build (3 OSes, warnings-as-errors).
2. Unit tests.
3. Oracle (netlist vs kicad-cli) on corpus.
4. Invariant + mutation + determinism + edge-case suites.
5. Sanitizers on corpus.
6. Output-format schema validation.
7. Performance thresholds.
A red on 1–5 blocks merge. 6–7 block release.

## 9. Manual QA before each release
- Run the KiCad plugin on 2–3 real modified projects; confirm navigation works.
- Run the GitHub Action on a test PR (one cosmetic, one connectivity change); confirm comment + gate.
- Run the browser WASM demo; confirm it matches the CLI.

## 10. Design-partner validation (beyond automated tests)
Before calling Phase 1 "done," have ≥2 design-partner teams run NetDiff on real PRs for two weeks and
report: did it catch a real change? did it false-alarm? Fix noise before scaling. Automated tests
prove correctness; design partners prove usefulness.
