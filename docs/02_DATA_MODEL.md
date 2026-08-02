# 02 — Data Model

This document defines the canonical data structures. Everything downstream (diff, output, UI)
depends on these being stable and versioned. Treat these schemas as contracts.

## 1. Design principles

- **Identity is explicit.** Every component and net has a stable identity so it can be matched
  across revisions. Never rely on array position for identity.
- **Coordinates are metadata, not identity.** Geometry is used to *resolve* connectivity, then
  largely discarded. Two schematics that draw the same circuit differently must produce equal
  connectivity graphs (modulo cosmetic fields). This is what makes the diff low-noise.
- **Everything serializes** to stable, versioned JSON. Ordering is deterministic (sort by stable
  keys before emit).

## 2. Core types

### 2.1 Pin
```
Pin {
  component_ref: string     // owning component refdes, e.g. "U3"
  number:        string     // pin number as in the symbol, e.g. "7" or "A12"
  name:          string     // pin name if available, e.g. "SDA" (may be empty)
  unit:          int        // for multi-unit parts (default 1)
}
PinId = "{component_ref}.{number}"   // e.g. "U3.7" — stable identity within a revision
```

### 2.2 Component
```
Component {
  ref:        string        // reference designator, e.g. "R5", "U3". Primary identity.
  value:      string        // e.g. "10k", "ESP32-WROOM"
  footprint:  string        // e.g. "Resistor_SMD:R_0402" (may be empty at schematic stage)
  lib_id:     string        // symbol library id, e.g. "Device:R"
  sheet_path: string        // hierarchical sheet path, e.g. "/power/" (root = "/")
  pins:       [Pin]
  // cosmetic (excluded from identity & from significant-diff):
  position:   {x, y, rotation}   // OPTIONAL, retained only for UI navigation
}
```
Notes:
- `ref` is the primary identity across revisions. Define `ComponentId = ref`.
  *(Resolved 2026-08-03, was `sheet_path + ref`.* KiCad annotates refdes uniquely per project —
  `kicad-cli` declares exactly one `comp` per ref, e.g. vme-wren exports 1507 comps for 1507 distinct
  refs — while a multi-unit part may spread its units over several sheets, four times in the corpus
  (`IC14` spans nine sheets). Keying on `sheet_path + ref` would therefore split one physical part
  into several components, disagree with KiCad's component count, and turn "a unit was moved to
  another sheet" into a spurious `ComponentRemoved` + `ComponentAdded` pair. If a future input ever
  does reuse a refdes for a different part, that is a broken annotation and should be reported, not
  silently accommodated.*)
- `sheet_path` is metadata identifying where the part's first unit sits; it is not part of the key.
- Multi-unit parts: units share a `ref` but have distinct pins; keep them under one Component —
  including when the units are drawn on different sheets. `PinId = ref.number`, so a pin number
  carried by several units (an op-amp's power pins) is one pin, not several.

### 2.3 Net
```
Net {
  name:       string        // resolved net name, e.g. "+3V3", "I2C_SDA".
                            //   May be an auto-generated name (e.g. "Net-(U3-Pad7)") — flag it.
  is_named:   bool          // true if the name came from a user label/power symbol
  is_power:   bool          // from a power symbol (GND, +3V3, ...)
  pins:       [PinId]       // sorted set of pins on this net — THIS is the net's true identity
  sheet_scope: string       // "global" or the sheet path for a local net
}
NetId = stable hash of the SORTED pin-set   // identity is the connectivity, not the name
```
Critical: a net's electrical identity is its **pin-set**, not its name. Names change; connectivity is
what matters. The diff uses both (see `03_DIFF_ALGORITHM.md`).

### 2.4 ConnectivityGraph (the canonical netlist)
```
ConnectivityGraph {
  schema_version: string          // e.g. "1.0" — bump on any breaking change
  source: {
    project_name: string
    entry_file:   string
    sheet_files:  [string]        // all sheets included
    revision:     string          // git sha or "working-tree", optional
  }
  components: [Component]          // sorted by ComponentId
  nets:       [Net]               // sorted by NetId
  stats: {
    component_count: int
    net_count:       int
    pin_count:       int
    unnamed_net_count: int
  }
}
```
Serialization: emit as JSON with keys sorted and arrays sorted by the identities above so output is
byte-stable. Include `schema_version` at the top.

## 3. Diff data structures

### 3.1 Change classification
```
Significance = SIGNIFICANT | COSMETIC
  SIGNIFICANT: connectivity/topology changed (a pin's net membership, a net's pin-set,
               a component's existence/value/footprint)
  COSMETIC:    only geometry/position/label-placement changed; connectivity identical
```

### 3.2 Change types
```
ComponentAdded    { component: Component }
ComponentRemoved  { component: Component }
ComponentModified { ref, changes: [{field, before, after}] }   // value/footprint/lib_id changes
NetAdded          { net: Net }
NetRemoved        { net: Net }
NetRenamed        { before_name, after_name, net: Net }        // same pin-set, different name
NetMerged         { before_nets: [name], after_net: name, pins_involved: [PinId] }
NetSplit          { before_net: name, after_nets: [name], pins_involved: [PinId] }
PinConnectionChanged {
  pin: PinId,
  before_net: string,   // net name the pin was on (or "unconnected")
  after_net:  string    // net name the pin is now on (or "unconnected")
}
```

### 3.3 DiffResult
```
DiffResult {
  schema_version: string
  before_ref: string
  after_ref:  string
  summary: {
    significant_count: int
    cosmetic_count:    int
    by_type: { ComponentAdded: n, PinConnectionChanged: n, ... }
    gate: PASS | FAIL          // per DiffConfig severity gate
  }
  changes: [ Change ]          // each Change has: type, significance, payload (one of the above),
                               //   and a human-readable message
}
```

## 4. Configuration (`.netdiff.yml`)
```
schema_version: "1.0"
gate:
  fail_on: significant          # significant | any | never
  ignore_change_types: []       # e.g. [ComponentModified] to not gate on value changes
net_normalization:
  case_insensitive: false
  aliases:                      # treat these names as equal (e.g. legacy renames)
    - [VCC, +5V]
ignore:
  components: []                # refdes globs to ignore, e.g. ["TP*"] test points
  nets: []                      # net-name globs to ignore
unnamed_net_matching:
  jaccard_threshold: 0.6        # min pin-set similarity to consider two unnamed nets "the same"
```

## 5. Versioning rules
- Any breaking change to `ConnectivityGraph` or `DiffResult` bumps `schema_version` (major).
- The CLI refuses to diff two graphs with incompatible major schema versions.
- Golden-corpus expected outputs are keyed by schema_version.
