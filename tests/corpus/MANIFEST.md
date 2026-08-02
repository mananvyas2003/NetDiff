# NetDiff golden corpus

License-compatible open-source KiCad projects used as the netlist correctness oracle
(`docs/06_TESTING_QA.md`). Pin-sets from `netdiff graph` must match
`kicad-cli sch export netlist` on every entry.

Do **not** add projects without recording source URL + license here.

## Sources

| Project | Source repo | Path / notes | License | Multi-sheet | Buses | No-connect |
|---------|-------------|--------------|---------|-------------|-------|------------|
| `complex_hierarchy` | [KiCad/kicad-source-mirror](https://github.com/KiCad/kicad-source-mirror) | `demos/complex_hierarchy` | CC BY-SA 4.0 | yes | | yes |
| `kit-dev-coldfire-xilinx_5213` | same | `demos/kit-dev-coldfire-xilinx_5213` | CC BY-SA 4.0 | yes | yes | yes |
| `video` | same | `demos/video` | CC BY-SA 4.0 | yes | yes | yes |
| `cm5_minima` | same | `demos/cm5_minima` | CC BY-SA 4.0 | yes | yes | yes |
| `royalblue54L_feather` | same | `demos/royalblue54L_feather` | CC BY-SA 4.0 | yes | yes | yes |
| `pic_programmer` | same | `demos/pic_programmer` | CC BY-SA 4.0 | yes | | yes |
| `tiny_tapeout` | same | `demos/tiny_tapeout` | CC BY-SA 4.0 | yes | | yes |
| `multichannel` | same | `demos/multichannel` | CC BY-SA 4.0 | yes | | |
| `openair-max` | same | `demos/openair-max` | CC BY-SA 4.0 | yes | | yes |
| `jetson-agx-thor-baseboard` | same | `demos/jetson-agx-thor-baseboard` | CC BY-SA 4.0 | yes | yes | yes |
| `vme-wren` | same | `demos/vme-wren` | CC BY-SA 4.0 | yes | yes | yes |
| `HALPI2` | [hatlabs/HALPI2-hardware](https://github.com/hatlabs/HALPI2-hardware) | project root | CERN-OHL-S v2 | yes | yes | yes |
| `interf_u` | KiCad demos | `demos/interf_u` | CC BY-SA 4.0 | no | yes | yes |
| `stickhub` | same | `demos/stickhub` | CC BY-SA 4.0 | no | | yes |
| `constraints` | same | `demos/constraints` | CC BY-SA 4.0 | no | | |
| `ecc83` | same | `demos/ecc83` (`ecc83-pp`) | CC BY-SA 4.0 | no | | yes |
| `simulation` | same | `demos/simulation` | CC BY-SA 4.0 | mixed | | |

KiCad demo license: `LICENSE.README` — “Licensed under CC BY-SA 4.0: All the demo files provided in demos/*”.

## Entry files (oracle roots)

| Corpus id | Entry `.kicad_sch` |
|-----------|-------------------|
| complex_hierarchy | `complex_hierarchy/complex_hierarchy.kicad_sch` |
| kit-dev-coldfire-xilinx_5213 | `kit-dev-coldfire-xilinx_5213/kit-dev-coldfire-xilinx_5213.kicad_sch` |
| video | `video/video.kicad_sch` |
| cm5_minima | `cm5_minima/CM5_MINIMA_3.kicad_sch` |
| royalblue54L_feather | `royalblue54L_feather/RoyalBlue54L-Feather.kicad_sch` |
| pic_programmer | `pic_programmer/pic_programmer.kicad_sch` |
| tiny_tapeout | `tiny_tapeout/tinytapeout-demo.kicad_sch` |
| multichannel | `multichannel/multichannel_mixer.kicad_sch` |
| openair-max | `openair-max/One-Air-Max.kicad_sch` |
| jetson-agx-thor-baseboard | `jetson-agx-thor-baseboard/jetson-agx-thor-baseboard.kicad_sch` |
| vme-wren | `vme-wren/vme-wren.kicad_sch` |
| HALPI2 | `HALPI2/HALPI2.kicad_sch` |
| interf_u | `interf_u/interf_u.kicad_sch` |
| stickhub | `stickhub/StickHub.kicad_sch` |
| ecc83 | `ecc83/ecc83-pp.kicad_sch` |
| simulation | each `*.kicad_sch` under `simulation/` treated as its own entry unless a root sheet is identified |

## Excluded

| Path | Reason |
|------|--------|
| `constraints/constraints.kicad_sch` | Empty KiCad 10.99 stub; `kicad-cli` 10.0 fails to load (no symbols) |
| `tests/fixtures/ESP32_Status_Monitor.kicad_sch` | Smoke fixture only; upstream lacks clear LICENSE |
| `demos/microwave` | No `.kicad_sch` |
| `tests/corpus/_staging/` | Local clone cache; gitignored |

## Attribution

Redistribution must retain CC BY-SA 4.0 / CERN-OHL-S notices as required.
