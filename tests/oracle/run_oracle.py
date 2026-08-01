#!/usr/bin/env python3
"""Netlist oracle: compare netdiff graph JSON pin-sets to kicad-cli export.

Exit codes:
  0 — all corpus projects pass
  1 — one or more mismatches / tool failures
  2 — kicad-cli not found (HARD FAILURE — never skip green)
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CORPUS = ROOT / "tests" / "corpus"
MANIFEST_ENTRIES = [
    ("complex_hierarchy", "complex_hierarchy/complex_hierarchy.kicad_sch"),
    ("kit-dev-coldfire-xilinx_5213", "kit-dev-coldfire-xilinx_5213/kit-dev-coldfire-xilinx_5213.kicad_sch"),
    ("video", "video/video.kicad_sch"),
    ("cm5_minima", "cm5_minima/CM5_MINIMA_3.kicad_sch"),
    ("royalblue54L_feather", "royalblue54L_feather/RoyalBlue54L-Feather.kicad_sch"),
    ("pic_programmer", "pic_programmer/pic_programmer.kicad_sch"),
    ("tiny_tapeout", "tiny_tapeout/tinytapeout-demo.kicad_sch"),
    ("multichannel", "multichannel/multichannel_mixer.kicad_sch"),
    ("openair-max", "openair-max/One-Air-Max.kicad_sch"),
    ("jetson-agx-thor-baseboard", "jetson-agx-thor-baseboard/jetson-agx-thor-baseboard.kicad_sch"),
    ("vme-wren", "vme-wren/vme-wren.kicad_sch"),
    ("HALPI2", "HALPI2/HALPI2.kicad_sch"),
    ("interf_u", "interf_u/interf_u.kicad_sch"),
    ("stickhub", "stickhub/StickHub.kicad_sch"),
    ("constraints", "constraints/constraints.kicad_sch"),
    ("ecc83", "ecc83/ecc83-pp.kicad_sch"),
]


def find_kicad_cli() -> str:
    env = os.environ.get("KICAD_CLI")
    if env and Path(env).is_file():
        return env
    which = shutil.which("kicad-cli")
    if which:
        return which
    candidates = [
        r"C:\Program Files\KiCad\10.0\bin\kicad-cli.exe",
        r"C:\Program Files\KiCad\9.0\bin\kicad-cli.exe",
        r"C:\Program Files\KiCad\8.0\bin\kicad-cli.exe",
        "/usr/bin/kicad-cli",
        "/usr/local/bin/kicad-cli",
        "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli",
    ]
    for c in candidates:
        if Path(c).is_file():
            return c
    return ""


def run(cmd: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, capture_output=True, text=True, check=False)


def export_kicad_netlist(kicad_cli: str, sch: Path, out_xml: Path) -> None:
    # Prefer XML sexpr/xml netlist; fall back across KiCad versions.
    attempts = [
        [kicad_cli, "sch", "export", "netlist", "--format", "kicadsexpr", "-o", str(out_xml), str(sch)],
        [kicad_cli, "sch", "export", "netlist", "--format", "kicadxml", "-o", str(out_xml), str(sch)],
        [kicad_cli, "sch", "export", "netlist", "-o", str(out_xml), str(sch)],
    ]
    last = None
    for cmd in attempts:
        last = run(cmd)
        if last.returncode == 0 and out_xml.is_file() and out_xml.stat().st_size > 0:
            return
    raise RuntimeError(
        f"kicad-cli netlist export failed for {sch}:\n"
        f"stdout={last.stdout if last else ''}\nstderr={last.stderr if last else ''}"
    )


def parse_kicad_xml_components_and_nets(xml_path: Path) -> tuple[set[str], dict[str, set[str]]]:
    """Return (component_refs, net_name -> pin_ids).

    KiCad XML netlist uses <comp ref=\"R1\"> and <net><node ref=\"R1\" pin=\"1\"/>.
    S-expression netlists are converted poorly by ElementTree; if parse fails, raise.
    """
    text = xml_path.read_text(encoding="utf-8", errors="replace")
    # Strip sexpr if accidentally produced — detect by leading '('
    if text.lstrip().startswith("("):
        return parse_kicad_sexpr_netlist(text)

    root = ET.fromstring(text)
    comps: set[str] = set()
    for comp in root.findall(".//comp"):
        ref = comp.attrib.get("ref")
        if ref and not ref.startswith("#"):
            comps.add(ref)

    nets: dict[str, set[str]] = {}
    for net in root.findall(".//net"):
        name = net.attrib.get("name") or ""
        pins: set[str] = set()
        for node in net.findall("node"):
            ref = node.attrib.get("ref", "")
            pin = node.attrib.get("pin", "")
            if ref and pin and not ref.startswith("#"):
                pins.add(f"{ref}.{pin}")
        if pins:
            nets[name] = pins
    return comps, nets


def parse_kicad_sexpr_netlist(text: str) -> tuple[set[str], dict[str, set[str]]]:
    """Minimal extraction from KiCad s-expr netlist export."""
    # Very small tokenizer for (comp (ref "R1")) and (net (name "GND") (node (ref "R1") (pin "1")))
    comps: set[str] = set()
    nets: dict[str, set[str]] = {}

    # components
    i = 0
    while True:
        j = text.find("(ref ", i)
        if j < 0:
            break
        # only count those under (comp ...) by looking back a bit
        window = text[max(0, j - 40) : j]
        if "(comp" in window or True:
            # parse "REF" or REF
            k = j + 5
            while k < len(text) and text[k].isspace():
                k += 1
            if k < len(text) and text[k] == '"':
                k2 = text.find('"', k + 1)
                ref = text[k + 1 : k2]
            else:
                k2 = k
                while k2 < len(text) and text[k2] not in " \n\r\t)":
                    k2 += 1
                ref = text[k:k2]
            if ref and not ref.startswith("#") and "(pin " not in window:
                # filter pin number refs: crude — skip if previous token looks like pin context
                if "(node" not in text[max(0, j - 80) : j]:
                    comps.add(ref)
        i = j + 5

    # nets: find (net ... (name ...) (node (ref ...) (pin ...))+)
    pos = 0
    while True:
        n = text.find("(net ", pos)
        if n < 0:
            break
        # find matching end roughly by next "(net " or end — fragile but OK for oracle v1
        n2 = text.find("(net ", n + 5)
        block = text[n : n2 if n2 > 0 else len(text)]
        name = ""
        nm = block.find("(name ")
        if nm >= 0:
            k = nm + 6
            while k < len(block) and block[k].isspace():
                k += 1
            if k < len(block) and block[k] == '"':
                k2 = block.find('"', k + 1)
                name = block[k + 1 : k2]
            else:
                k2 = k
                while k2 < len(block) and block[k2] not in " \n\r\t)":
                    k2 += 1
                name = block[k:k2]
        pins: set[str] = set()
        p = 0
        while True:
            nr = block.find("(ref ", p)
            if nr < 0:
                break
            # must be inside node
            if "(node" not in block[max(0, nr - 60) : nr]:
                p = nr + 5
                continue
            k = nr + 5
            while k < len(block) and block[k].isspace():
                k += 1
            if k < len(block) and block[k] == '"':
                k2 = block.find('"', k + 1)
                ref = block[k + 1 : k2]
                k = k2 + 1
            else:
                k2 = k
                while k2 < len(block) and block[k2] not in " \n\r\t)":
                    k2 += 1
                ref = block[k:k2]
                k = k2
            np = block.find("(pin ", k)
            pin = ""
            if np >= 0 and np < k + 80:
                k = np + 5
                while k < len(block) and block[k].isspace():
                    k += 1
                if k < len(block) and block[k] == '"':
                    k2 = block.find('"', k + 1)
                    pin = block[k + 1 : k2]
                else:
                    k2 = k
                    while k2 < len(block) and block[k2] not in " \n\r\t)":
                        k2 += 1
                    pin = block[k:k2]
            if ref and pin and not ref.startswith("#"):
                pins.add(f"{ref}.{pin}")
            p = nr + 5
        if pins:
            nets[name] = pins
        pos = n + 5

    return comps, nets


def load_netdiff_graph(netdiff_bin: str, sch: Path) -> dict:
    proc = run([netdiff_bin, str(sch)])
    if proc.returncode != 0:
        raise RuntimeError(f"netdiff failed ({proc.returncode}): {proc.stderr}")
    return json.loads(proc.stdout)


def graph_components_and_nets(g: dict) -> tuple[set[str], dict[str, set[str]]]:
    comps = {c["ref"] for c in g.get("components", []) if not c["ref"].startswith("#")}
    # Prefer pin-set identity: key nets by frozenset of pins for comparison,
    # but also keep name map for diagnostics.
    nets: dict[str, set[str]] = {}
    for n in g.get("nets", []):
        pins = set(n.get("pins", []))
        # Strip sheet_path prefixes from pin ids if present? PinId is ref.number in schema.
        if pins:
            nets[n.get("name", "")] = pins
    return comps, nets


def compare_pinsets(
    expected_nets: dict[str, set[str]], actual_nets: dict[str, set[str]]
) -> list[str]:
    """Match nets by pin-set equality (names may differ)."""
    errors: list[str] = []
    exp_sets = {frozenset(v): k for k, v in expected_nets.items()}
    act_sets = {frozenset(v): k for k, v in actual_nets.items()}

    missing = exp_sets.keys() - act_sets.keys()
    extra = act_sets.keys() - exp_sets.keys()
    for s in sorted(missing, key=lambda x: (len(x), sorted(x)[:3])):
        errors.append(f"missing pin-set (kicad net '{exp_sets[s]}'): {sorted(s)[:12]}...")
    for s in sorted(extra, key=lambda x: (len(x), sorted(x)[:3])):
        errors.append(f"extra pin-set (netdiff net '{act_sets[s]}'): {sorted(s)[:12]}...")
    return errors


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--netdiff", default=os.environ.get("NETDIFF_BIN", ""))
    ap.add_argument("--only", action="append", default=[])
    ap.add_argument("--fast", action="store_true", help="subset of multi-sheet + flats")
    args = ap.parse_args()

    kicad_cli = find_kicad_cli()
    if not kicad_cli:
        print("FAIL: kicad-cli not found (oracle hard requirement)", file=sys.stderr)
        return 2

    netdiff_bin = args.netdiff
    if not netdiff_bin:
        for c in [
            ROOT / "build" / "engine" / "netdiff_engine.exe",
            ROOT / "build" / "engine" / "netdiff_engine",
            ROOT / "build" / "engine" / "Release" / "netdiff_engine.exe",
        ]:
            if c.is_file():
                netdiff_bin = str(c)
                break
    if not netdiff_bin or not Path(netdiff_bin).is_file():
        print("FAIL: netdiff engine binary not found; pass --netdiff", file=sys.stderr)
        return 1

    entries = MANIFEST_ENTRIES
    if args.fast:
        fast_ids = {
            "complex_hierarchy",
            "kit-dev-coldfire-xilinx_5213",
            "pic_programmer",
            "HALPI2",
            "interf_u",
            "stickhub",
            "ecc83",
        }
        entries = [e for e in entries if e[0] in fast_ids]
    if args.only:
        entries = [e for e in entries if e[0] in set(args.only)]

    print(f"kicad-cli: {kicad_cli}")
    print(f"netdiff:   {netdiff_bin}")
    failed = 0
    with tempfile.TemporaryDirectory(prefix="netdiff-oracle-") as td:
        tdir = Path(td)
        for cid, rel in entries:
            sch = CORPUS / rel
            print(f"\n=== {cid} ===")
            if not sch.is_file():
                print(f"FAIL: missing {sch}")
                failed += 1
                continue
            try:
                xml_path = tdir / f"{cid}.net"
                export_kicad_netlist(kicad_cli, sch, xml_path)
                exp_comps, exp_nets = parse_kicad_xml_components_and_nets(xml_path)
                graph = load_netdiff_graph(netdiff_bin, sch)
                act_comps, act_nets = graph_components_and_nets(graph)

                # Component set: compare refs only (hierarchical path may differ in naming).
                # Prefer suffix match: actual may be sheet_path+ref in future — compare bare refs.
                act_refs = {c.split("/")[-1] if "/" in c else c for c in act_comps}
                # components in graph use "ref" field already bare
                act_refs = set(act_comps)

                comp_missing = exp_comps - act_refs
                comp_extra = act_refs - exp_comps
                errors: list[str] = []
                if comp_missing:
                    errors.append(f"components missing: {sorted(comp_missing)[:20]}")
                if comp_extra:
                    errors.append(f"components extra: {sorted(comp_extra)[:20]}")

                # Normalize pin ids: if netdiff uses sheet_path in pin id, strip to ref.pin
                def norm_pins(nets: dict[str, set[str]]) -> dict[str, set[str]]:
                    out: dict[str, set[str]] = {}
                    for name, pins in nets.items():
                        npins = set()
                        for p in pins:
                            # PinId form ComponentRef.Number; ComponentRef may include path
                            if "/" in p:
                                # /path/R1.1 -> R1.1
                                bare = p.rsplit("/", 1)[-1]
                                npins.add(bare)
                            else:
                                npins.add(p)
                        out[name] = npins
                    return out

                errors.extend(compare_pinsets(norm_pins(exp_nets), norm_pins(act_nets)))
                if errors:
                    print("FAIL")
                    for e in errors[:30]:
                        print("  ", e)
                    failed += 1
                else:
                    print(
                        f"PASS  comps={len(act_refs)} nets={len(act_nets)} "
                        f"(kicad nets={len(exp_nets)})"
                    )
            except Exception as ex:  # noqa: BLE001 — report per-project
                print(f"FAIL: {ex}")
                failed += 1

    print("\n========")
    if failed:
        print(f"FAILED: {failed} project(s)")
        return 1
    print("ALL PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
