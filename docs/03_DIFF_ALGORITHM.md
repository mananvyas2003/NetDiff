# 03 — Semantic Diff Algorithm (Core IP)

This is the heart of the product. A visual diff compares pictures; this compares circuits. Implement
it as a pure, deterministic function:

```
DiffResult Diff(const ConnectivityGraph& A /*before*/,
                const ConnectivityGraph& B /*after*/,
                const DiffConfig& cfg);
```

## 1. Overview of the pipeline

```
1. Normalize both graphs (apply cfg: aliases, ignore lists, case rules).
2. Match components  A↔B  → added / removed / modified.
3. Match nets        A↔B  → by name, then by pin-set similarity for unnamed/renamed.
4. Derive pin-level connectivity changes from the net matching.
5. Detect net merges and splits.
6. Classify every change SIGNIFICANT vs COSMETIC.
7. Apply the config gate → PASS/FAIL.
8. Emit deterministic, sorted DiffResult.
```

## 2. Step 1 — Normalization

- Apply `net_normalization.aliases`: rewrite aliased names to a canonical form in both graphs.
- Apply `net_normalization.case_insensitive` if set.
- Drop ignored components/nets (`ignore.components`, `ignore.nets` globs) from both graphs.
- Do **not** alter pin-sets during normalization — only names and membership of the ignore set.

## 3. Step 2 — Component matching

Primary key: `ComponentId = sheet_path + ref` (see `02_DATA_MODEL.md`).

```
matched     = { c in A.components : exists c' in B with same ComponentId }
removed     = A.components with ComponentId not in B
added       = B.components with ComponentId not in A
for each matched pair (a, b):
    field_changes = diff of {value, footprint, lib_id}
    if field_changes non-empty → ComponentModified(ref, field_changes)  [SIGNIFICANT]
```

Refdes-reuse guard: if the same `ComponentId` maps to components with **different `lib_id`**
(e.g. a resistor became a capacitor at the same refdes), treat as Removed + Added rather than
Modified, and flag it as a high-severity change (likely a mistake).

Optional heuristic (config-gated, default off): if a component appears removed in A and added in B
with identical `lib_id`, `value`, and pin-set but a *different* refdes, report it as a possible
**ComponentRenamed** rather than remove+add. Keep this off by default to avoid false matches.

## 4. Step 3 — Net matching (the subtle part)

A net's true identity is its **sorted pin-set**, not its name. Match in tiers:

### Tier 1 — exact name match
Nets with equal `name` in A and B are candidate matches. For each such pair, compare pin-sets:
- Equal pin-sets → **unchanged net** (no change emitted; may still be COSMETIC elsewhere).
- Different pin-sets → the pin-level differences flow to Step 4 (pins added/removed on this net).

### Tier 2 — same pin-set, different name → NetRenamed
For nets not matched by name, index them by `NetId` (hash of sorted pin-set). If an A-net and a
B-net share the exact pin-set but differ in `name`:
- Emit **NetRenamed(before_name, after_name)**.
- Significance: SIGNIFICANT if either name `is_named` (a user-facing net was renamed — reviewers
  care); COSMETIC if both are auto-generated names and nothing else changed. (Config can force
  either.)

### Tier 3 — fuzzy match for unnamed / restructured nets
Remaining unmatched nets (typical for auto-named local nets that gained/lost a pin) are matched by
pin-set similarity:
```
similarity(n1, n2) = |pins(n1) ∩ pins(n2)| / |pins(n1) ∪ pins(n2)|   // Jaccard
```
Greedily match the highest-similarity pairs above `cfg.unnamed_net_matching.jaccard_threshold`
(default 0.6), each net used at most once. Matched pairs feed pin-level changes to Step 4.
Unmatched A-nets → **NetRemoved**; unmatched B-nets → **NetAdded**.

Determinism: when similarities tie, break ties by (a) name equality, then (b) lexicographic NetId.
Never depend on iteration order of a hash map.

## 5. Step 4 — Pin-level connectivity changes

For every pin present in both revisions (i.e. on a matched component), determine the net it belongs
to in A and in B (by name of the matched net; "unconnected" if on no net):
```
for each PinId p in (pins(A) ∩ pins(B)):
    net_before = name of p's net in A   (or "unconnected")
    net_after  = name of p's net in B   (or "unconnected")
    if net_before != net_after:
        emit PinConnectionChanged(p, net_before, net_after)   [SIGNIFICANT]
```
This is the highest-signal change type — it catches "pin re-tied to the wrong rail," which is
exactly the class of bug visual diff misses. Every `PinConnectionChanged` is SIGNIFICANT.

## 6. Step 5 — Net merge / split detection

Derive from the pin-level view:
- **Merge:** two or more distinct A-nets whose pins all end up on a single B-net → **NetMerged**.
  (e.g. a wire accidentally shorts two rails.) Always SIGNIFICANT; flag power-net merges (GND+VCC)
  as critical.
- **Split:** one A-net whose pins are distributed across multiple B-nets → **NetSplit**. Always
  SIGNIFICANT.

Detect by building the bipartite mapping between A-nets and B-nets induced by shared pins, then
finding components of that bipartite graph: a component with (many A-nets → 1 B-net) is a merge;
(1 A-net → many B-nets) is a split; (1→1) is a rename/unchanged.

## 7. Step 6 — Significance classification (summary table)

| Change | Significance |
|--------|--------------|
| PinConnectionChanged | SIGNIFICANT (always) |
| NetMerged / NetSplit | SIGNIFICANT (always); power-net merge = CRITICAL |
| NetAdded / NetRemoved | SIGNIFICANT |
| NetRenamed | SIGNIFICANT if user-named; else COSMETIC |
| ComponentAdded / Removed | SIGNIFICANT |
| ComponentModified (value/footprint/lib_id) | SIGNIFICANT |
| position/rotation/label-placement only | COSMETIC (never gates) |

The core guarantee: if two revisions are electrically identical (same components, same net pin-sets,
same names), `DiffResult.summary.significant_count == 0`, regardless of how the drawing moved. This
is the property to test hardest (see `06_TESTING_QA.md`).

## 8. Step 7 — Gate
```
gate = FAIL if (cfg.gate.fail_on == "any"        and total_changes > 0)
            or (cfg.gate.fail_on == "significant" and significant_count > 0
                after removing cfg.gate.ignore_change_types)
     else PASS
```

## 9. Step 8 — Deterministic emission
- Sort `changes` by (significance desc, type, then a stable key: PinId / net name / ComponentId).
- Every run on identical input produces byte-identical `DiffResult` JSON.
- Human-readable `message` per change, e.g.:
  - `"U3.7 moved from GND to +3V3"` (PinConnectionChanged)
  - `"Nets +5V and +3V3 merged into +5V (via C12.1)"` (NetMerged, CRITICAL)
  - `"Net renamed: Net-(R4-Pad2) → I2C_SDA"` (NetRenamed)
  - `"R17 (10k) removed"` (ComponentRemoved)

## 10. Complexity & performance
- Component match: hash-join on ComponentId → O(n).
- Net name match: hash-join → O(m).
- Fuzzy net match: only over *unmatched* nets, which is normally small; cap with the Jaccard
  threshold and a candidate index (bucket nets by shared pins) to avoid O(m²) blowup on large boards.
- Target: < 500 ms for 150-component boards; < 5 s for 1000-component multi-sheet designs.

## 11. Edge cases to handle explicitly (write tests for each)
- No-connect flags (a pin intentionally unconnected must not read as "unconnected error").
- Buses and bus members (expand to member nets before diffing).
- Global vs local labels with the same name on different sheets.
- Power symbols creating implicit global nets.
- Multi-unit components (pins across units share a refdes).
- A net that exists in one revision only because a whole sheet was added/removed.
- Empty schematic / single-component schematic / schematic with no nets.
