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
import re
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
    # constraints demo in KiCad 10.99 is an empty stub; omitted (kicad-cli 10.0 cannot load it).
    ("ecc83", "ecc83/ecc83-pp.kicad_sch"),
]


def sheet_count_for(cid: str) -> int:
    """Number of .kicad_sch files under the corpus project directory."""
    project_dir = CORPUS / cid
    if not project_dir.is_dir():
        return 0
    return sum(1 for _ in project_dir.rglob("*.kicad_sch"))


def pin_count(nets: dict[str, set[str]]) -> int:
    return sum(len(pins) for pins in nets.values())


def write_correctness_report(path: Path, designs: list[dict], meta: dict) -> None:
    """Machine-readable correctness report (pass/fail per design + counts)."""
    failed = sum(1 for d in designs if d["status"] == "FAIL")
    passed = sum(1 for d in designs if d["status"] == "PASS")
    report = {
        "schema_version": "1.0",
        "kind": "netdiff_correctness_report",
        "passed": failed == 0 and passed > 0,
        "failed": failed,
        "passed_count": passed,
        "design_count": len(designs),
        "kicad_cli": meta.get("kicad_cli", ""),
        "netdiff": meta.get("netdiff", ""),
        "designs": designs,
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def find_kicad_cli() -> str:
    env = os.environ.get("KICAD_CLI")
    if env and Path(env).is_file():
        return env
    which = shutil.which("kicad-cli")
    if which:
        return which
    candidates = [
        r"d:\Practice\tools\kicad-extract\bin\kicad-cli.exe",
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
    return subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )


def export_kicad_netlist(kicad_cli: str, sch: Path, out_path: Path) -> None:
    attempts = [
        [kicad_cli, "sch", "export", "netlist", "--format", "kicadsexpr", "-o", str(out_path), str(sch)],
        [kicad_cli, "sch", "export", "netlist", "--format", "kicadxml", "-o", str(out_path), str(sch)],
        [kicad_cli, "sch", "export", "netlist", "-o", str(out_path), str(sch)],
    ]
    last = None
    for cmd in attempts:
        last = run(cmd)
        if last.returncode == 0 and out_path.is_file() and out_path.stat().st_size > 0:
            return
    raise RuntimeError(
        f"kicad-cli netlist export failed for {sch}:\n"
        f"stdout={last.stdout if last else ''}\nstderr={last.stderr if last else ''}"
    )


def _atom(s: str) -> str:
    s = s.strip()
    if len(s) >= 2 and s[0] == '"' and s[-1] == '"':
        return s[1:-1]
    return s


def parse_kicad_sexpr_netlist(text: str) -> tuple[set[str], dict[str, set[str]]]:
    """Parse KiCad `kicadsexpr` export netlist."""
    comps: set[str] = set()
    # Components: (comp (ref "R1") ...)
    for m in re.finditer(r'\(comp\b[\s\S]*?\(ref\s+("(?:\\.|[^"])*"|[^\s()]+)\)', text):
        ref = _atom(m.group(1))
        if ref and not ref.startswith("#"):
            comps.add(ref)

    nets: dict[str, set[str]] = {}
    # Extract (nets ... ) block if present
    nets_m = re.search(r'\(nets\b', text)
    body = text[nets_m.start() :] if nets_m else text

    # Each net: (net (code ...) (name "GND") ... (node (ref "C1") (pin "1") ...) ...)
    for net_m in re.finditer(r'\(net\b', body):
        start = net_m.start()
        # find matching close by paren depth
        depth = 0
        i = start
        end = -1
        while i < len(body):
            ch = body[i]
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
                if depth == 0:
                    end = i + 1
                    break
            i += 1
        if end < 0:
            continue
        block = body[start:end]
        name_m = re.search(r'\(name\s+("(?:\\.|[^"])*"|[^\s()]+)\)', block)
        if not name_m:
            continue
        name = _atom(name_m.group(1))
        pins: set[str] = set()
        for node_m in re.finditer(
            r'\(node\b[\s\S]*?\(ref\s+("(?:\\.|[^"])*"|[^\s()]+)\)[\s\S]*?\(pin\s+("(?:\\.|[^"])*"|[^\s()]+)\)',
            block,
        ):
            ref = _atom(node_m.group(1))
            pin = _atom(node_m.group(2))
            if ref and pin and not ref.startswith("#"):
                pins.add(f"{ref}.{pin}")
        if pins:
            nets[name] = pins
    return comps, nets


def parse_kicad_xml_components_and_nets(path: Path) -> tuple[set[str], dict[str, set[str]]]:
    text = path.read_text(encoding="utf-8", errors="replace")
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


def load_netdiff_graph(netdiff_bin: str, sch: Path) -> dict:
    proc = run([netdiff_bin, str(sch)])
    if proc.returncode != 0:
        raise RuntimeError(f"netdiff failed ({proc.returncode}): {proc.stderr}")
    return json.loads(proc.stdout)


def is_power_or_flag_ref(ref: str) -> bool:
    return ref.startswith("#")


def normalize_pin_id(pid: str) -> str | None:
    """Strip sheet path prefixes; drop power/flag pins."""
    bare = pid.rsplit("/", 1)[-1] if "/" in pid else pid
    # PinId = Ref.Number — ref may itself contain dots rarely; split once from right for number
    if "." not in bare:
        return None
    ref, num = bare.rsplit(".", 1)
    # hierarchical ComponentId in pin unlikely; ref is designator
    if is_power_or_flag_ref(ref):
        return None
    return f"{ref}.{num}"


def graph_components_and_nets(g: dict) -> tuple[set[str], dict[str, set[str]]]:
    comps = {
        c["ref"]
        for c in g.get("components", [])
        if c.get("ref") and not is_power_or_flag_ref(c["ref"])
    }
    nets: dict[str, set[str]] = {}
    anon = 0
    for n in g.get("nets", []):
        pins: set[str] = set()
        for p in n.get("pins", []):
            np = normalize_pin_id(p)
            if np:
                pins.add(np)
        if not pins:
            continue
        name = n.get("name", "") or ""
        # Unnamed / duplicate names must not overwrite — oracle compares pin-sets.
        key = name
        if key in nets:
            anon += 1
            key = f"{name}__anon{anon}"
        nets[key] = pins
    return comps, nets


def compare_pinsets(
    expected_nets: dict[str, set[str]], actual_nets: dict[str, set[str]]
) -> list[str]:
    errors: list[str] = []
    exp_sets = {frozenset(v): k for k, v in expected_nets.items()}
    act_sets = {frozenset(v): k for k, v in actual_nets.items()}

    missing = exp_sets.keys() - act_sets.keys()
    extra = act_sets.keys() - exp_sets.keys()
    for s in sorted(missing, key=lambda x: (len(x), sorted(x)[:3])):
        sample = sorted(s)[:12]
        errors.append(f"missing pin-set (kicad net '{exp_sets[s]}'): {sample}")
    for s in sorted(extra, key=lambda x: (len(x), sorted(x)[:3])):
        sample = sorted(s)[:12]
        errors.append(f"extra pin-set (netdiff net '{act_sets[s]}'): {sample}")
    return errors


def main() -> int:
    ap = argparse.ArgumentParser(
        description="NetDiff ↔ kicad-cli netlist correctness oracle"
    )
    ap.add_argument("--netdiff", default=os.environ.get("NETDIFF_BIN", ""))
    ap.add_argument("--only", action="append", default=[])
    ap.add_argument("--fast", action="store_true", help="subset of multi-sheet + flats")
    ap.add_argument(
        "--report",
        type=Path,
        default=None,
        help="Write machine-readable correctness report JSON to this path",
    )
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
        entries = [e for e in entries if e[0] in set(fast_ids)]
    if args.only:
        entries = [e for e in entries if e[0] in set(args.only)]

    print(f"kicad-cli: {kicad_cli}")
    print(f"netdiff:   {netdiff_bin}")
    designs: list[dict] = []
    failed = 0
    with tempfile.TemporaryDirectory(prefix="netdiff-oracle-") as td:
        tdir = Path(td)
        for cid, rel in entries:
            sch = CORPUS / rel
            sheets = sheet_count_for(cid)
            print(f"\n=== {cid} ===")
            row: dict = {
                "id": cid,
                "entry": rel.replace("\\", "/"),
                "sheet_count": sheets,
                "status": "FAIL",
                "components_kicad": 0,
                "components_netdiff": 0,
                "nets_kicad": 0,
                "nets_netdiff": 0,
                "pins_kicad": 0,
                "pins_netdiff": 0,
                "errors": [],
            }
            if not sch.is_file():
                msg = f"missing {sch}"
                print(f"FAIL: {msg}")
                row["errors"] = [msg]
                designs.append(row)
                failed += 1
                continue
            try:
                net_path = tdir / f"{cid}.net"
                export_kicad_netlist(kicad_cli, sch, net_path)
                exp_comps, exp_nets = parse_kicad_xml_components_and_nets(net_path)
                graph = load_netdiff_graph(netdiff_bin, sch)
                act_comps, act_nets = graph_components_and_nets(graph)

                row["components_kicad"] = len(exp_comps)
                row["components_netdiff"] = len(act_comps)
                row["nets_kicad"] = len(exp_nets)
                row["nets_netdiff"] = len(act_nets)
                row["pins_kicad"] = pin_count(exp_nets)
                row["pins_netdiff"] = pin_count(act_nets)

                errors: list[str] = []
                comp_missing = exp_comps - act_comps
                comp_extra = act_comps - exp_comps
                if comp_missing:
                    errors.append(f"components missing: {sorted(comp_missing)[:20]}")
                if comp_extra:
                    errors.append(f"components extra: {sorted(comp_extra)[:20]}")

                errors.extend(compare_pinsets(exp_nets, act_nets))
                if errors:
                    print("FAIL")
                    for e in errors[:40]:
                        print("  ", e)
                    row["errors"] = errors
                    designs.append(row)
                    failed += 1
                else:
                    print(
                        f"PASS  comps={len(act_comps)} nets={len(act_nets)} "
                        f"(kicad comps={len(exp_comps)} nets={len(exp_nets)}) "
                        f"sheets={sheets}"
                    )
                    row["status"] = "PASS"
                    row["errors"] = []
                    designs.append(row)
            except Exception as ex:  # noqa: BLE001
                print(f"FAIL: {ex}")
                row["errors"] = [str(ex)]
                designs.append(row)
                failed += 1

    if args.report is not None:
        write_correctness_report(
            args.report,
            designs,
            {"kicad_cli": kicad_cli, "netdiff": netdiff_bin},
        )
        print(f"\nWrote correctness report: {args.report}")

    print("\n========")
    if failed:
        print(f"FAILED: {failed} project(s)")
        return 1
    print("ALL PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
