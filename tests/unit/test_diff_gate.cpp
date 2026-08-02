// T1.5: significance classification (03 §7) and the gate (03 §8).

#include "netdiff/diff.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

using netdiff::Change;
using netdiff::ChangeType;
using netdiff::Component;
using netdiff::ConnectivityGraph;
using netdiff::Diff;
using netdiff::DiffConfig;
using netdiff::DiffResult;
using netdiff::GateResult;
using netdiff::Net;
using netdiff::Pin;
using netdiff::Significance;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << "\n";
        ++g_failures;
    }
}

Net MakeNet(const std::string& name, bool is_named, bool is_power,
            std::vector<std::string> pins) {
    Net n;
    n.name = name;
    n.is_named = is_named;
    n.is_power = is_power;
    std::sort(pins.begin(), pins.end());
    n.pins = std::move(pins);
    for (const auto& p : n.pins) {
        n.net_id += p;
        n.net_id += "|";
    }
    return n;
}

ConnectivityGraph MakeGraph(std::vector<Net> nets, std::vector<Component> extra = {}) {
    ConnectivityGraph g;
    g.source.revision = "rev";
    g.nets = std::move(nets);
    std::vector<std::pair<std::string, std::string>> refs;
    for (const auto& net : g.nets) {
        for (const auto& pin_id : net.pins) {
            const auto dot = pin_id.rfind('.');
            refs.emplace_back(pin_id.substr(0, dot), pin_id.substr(dot + 1));
        }
    }
    std::sort(refs.begin(), refs.end());
    refs.erase(std::unique(refs.begin(), refs.end()), refs.end());
    for (const auto& entry : refs) {
        if (g.components.empty() || g.components.back().ref != entry.first) {
            Component c;
            c.ref = entry.first;
            c.lib_id = "Lib:Part";
            g.components.push_back(c);
        }
        Pin p;
        p.component_ref = entry.first;
        p.number = entry.second;
        g.components.back().pins.push_back(p);
    }
    for (auto& c : extra) {
        g.components.push_back(std::move(c));
    }
    std::sort(g.components.begin(), g.components.end(),
              [](const Component& a, const Component& b) { return a.ref < b.ref; });
    return g;
}

Component MakePart(const std::string& ref, const std::string& value,
                   const std::string& lib_id) {
    Component c;
    c.ref = ref;
    c.value = value;
    c.lib_id = lib_id;
    return c;
}

Significance SignificanceOf(const DiffResult& result, ChangeType type) {
    for (const auto& c : result.changes) {
        if (c.type() == type) {
            return c.significance;
        }
    }
    // Nothing of that type: report the opposite of what any caller asserts so
    // a missing change cannot pass silently.
    Check(false, std::string("expected a ") + netdiff::ToString(type) + " change");
    return Significance::kCosmetic;
}

const DiffConfig kDefault{};

// 03 §7's table, one row at a time.
void TestSignificanceTable() {
    {  // PinConnectionChanged — always SIGNIFICANT
        const auto a = MakeGraph({MakeNet("GND", true, true, {"U1.1", "U2.1"}),
                                  MakeNet("VCC", true, true, {"U3.1"})});
        const auto b = MakeGraph({MakeNet("GND", true, true, {"U1.1"}),
                                  MakeNet("VCC", true, true, {"U3.1", "U2.1"})});
        const auto r = Diff(a, b, kDefault);
        Check(SignificanceOf(r, ChangeType::kPinConnectionChanged) ==
                  Significance::kSignificant,
              "table: PinConnectionChanged is SIGNIFICANT");
    }
    {  // NetMerged / NetSplit — always SIGNIFICANT
        const auto a = MakeGraph({MakeNet("A", true, false, {"U1.1", "U1.2"}),
                                  MakeNet("B", true, false, {"U2.1", "U2.2"})});
        const auto b = MakeGraph(
            {MakeNet("A", true, false, {"U1.1", "U1.2", "U2.1", "U2.2"})});
        Check(SignificanceOf(Diff(a, b, kDefault), ChangeType::kNetMerged) ==
                  Significance::kSignificant,
              "table: NetMerged is SIGNIFICANT");
        Check(SignificanceOf(Diff(b, a, kDefault), ChangeType::kNetSplit) ==
                  Significance::kSignificant,
              "table: NetSplit is SIGNIFICANT");
    }
    {  // NetAdded / NetRemoved — SIGNIFICANT
        const auto a = MakeGraph({MakeNet("OLD", true, false, {"U1.1", "U1.2"})});
        const auto b = MakeGraph({MakeNet("NEW", true, false, {"U8.1", "U8.2"})});
        const auto r = Diff(a, b, kDefault);
        Check(SignificanceOf(r, ChangeType::kNetAdded) == Significance::kSignificant,
              "table: NetAdded is SIGNIFICANT");
        Check(SignificanceOf(r, ChangeType::kNetRemoved) == Significance::kSignificant,
              "table: NetRemoved is SIGNIFICANT");
    }
    {  // NetRenamed — SIGNIFICANT if user-named, else COSMETIC
        const auto named_a = MakeGraph({MakeNet("SDA", true, false, {"U1.1", "U1.2"})});
        const auto named_b = MakeGraph({MakeNet("I2C_SDA", true, false, {"U1.1", "U1.2"})});
        Check(SignificanceOf(Diff(named_a, named_b, kDefault), ChangeType::kNetRenamed) ==
                  Significance::kSignificant,
              "table: NetRenamed of a user-named net is SIGNIFICANT");

        const auto auto_a =
            MakeGraph({MakeNet("Net-(U1-Pad1)", false, false, {"U1.1", "U1.2"})});
        const auto auto_b =
            MakeGraph({MakeNet("Net-(U1-Pad2)", false, false, {"U1.1", "U1.2"})});
        Check(SignificanceOf(Diff(auto_a, auto_b, kDefault), ChangeType::kNetRenamed) ==
                  Significance::kCosmetic,
              "table: NetRenamed of auto-named nets is COSMETIC");
    }
    {  // ComponentAdded / Removed / Modified — SIGNIFICANT
        const auto a = MakeGraph({MakeNet("N", true, false, {"U1.1", "U1.2"})},
                                 {MakePart("R1", "10k", "Device:R")});
        const auto b = MakeGraph({MakeNet("N", true, false, {"U1.1", "U1.2"})},
                                 {MakePart("R1", "4k7", "Device:R"),
                                  MakePart("R2", "1k", "Device:R")});
        const auto r = Diff(a, b, kDefault);
        Check(SignificanceOf(r, ChangeType::kComponentModified) == Significance::kSignificant,
              "table: ComponentModified is SIGNIFICANT");
        Check(SignificanceOf(r, ChangeType::kComponentAdded) == Significance::kSignificant,
              "table: ComponentAdded is SIGNIFICANT");
        Check(SignificanceOf(Diff(b, a, kDefault), ChangeType::kComponentRemoved) ==
                  Significance::kSignificant,
              "table: ComponentRemoved is SIGNIFICANT");
    }
}

// Position/rotation only — never even reaches the change list (03 §7 last row).
void TestPositionIsNeverAChange() {
    auto part = MakePart("R1", "10k", "Device:R");
    part.x = 10.0;
    part.y = 20.0;
    const auto a = MakeGraph({MakeNet("N", true, false, {"U1.1", "U1.2"})}, {part});
    part.x = 999.0;
    part.y = -42.5;
    part.rotation = 270.0;
    part.sheet_path = "/elsewhere/";
    const auto b = MakeGraph({MakeNet("N", true, false, {"U1.1", "U1.2"})}, {part});

    const auto r = Diff(a, b, kDefault);
    Check(r.changes.empty(), "position/rotation/sheet changes emit nothing");
    Check(r.summary.significant_count == 0, "position-only: significant_count == 0");
    Check(r.summary.gate == GateResult::kPass, "position-only: gate PASS");
}

void TestGateDefaultSignificant() {
    const auto a = MakeGraph({MakeNet("GND", true, true, {"U1.1", "U2.1"}),
                              MakeNet("VCC", true, true, {"U3.1"})});
    const auto b = MakeGraph({MakeNet("GND", true, true, {"U1.1"}),
                              MakeNet("VCC", true, true, {"U3.1", "U2.1"})});
    Check(Diff(a, b, kDefault).summary.gate == GateResult::kFail,
          "gate: a significant change FAILs by default");
    Check(Diff(a, a, kDefault).summary.gate == GateResult::kPass,
          "gate: no change PASSes");
}

// A cosmetic-only diff must not gate, whatever else is true.
void TestGateCosmeticOnly() {
    const auto a = MakeGraph({MakeNet("Net-(U1-Pad1)", false, false, {"U1.1", "U1.2"})});
    const auto b = MakeGraph({MakeNet("Net-(U1-Pad2)", false, false, {"U1.1", "U1.2"})});
    const auto r = Diff(a, b, kDefault);
    Check(r.summary.cosmetic_count == 1 && r.summary.significant_count == 0,
          "gate: fixture is cosmetic-only");
    Check(r.summary.gate == GateResult::kPass, "gate: cosmetic change PASSes by default");

    DiffConfig any;
    any.gate.fail_on = DiffConfig::Gate::FailOn::kAny;
    Check(Diff(a, b, any).summary.gate == GateResult::kFail,
          "gate: fail_on=any FAILs on a cosmetic change");
}

void TestGateNever() {
    const auto a = MakeGraph({MakeNet("GND", true, true, {"U1.1", "U2.1"}),
                              MakeNet("VCC", true, true, {"U3.1"})});
    const auto b = MakeGraph({MakeNet("GND", true, true, {"U1.1"}),
                              MakeNet("VCC", true, true, {"U3.1", "U2.1"})});
    DiffConfig never;
    never.gate.fail_on = DiffConfig::Gate::FailOn::kNever;
    const auto r = Diff(a, b, never);
    Check(r.summary.gate == GateResult::kPass, "gate: fail_on=never always PASSes");
    Check(r.summary.significant_count > 0,
          "gate: fail_on=never still reports the changes it does not gate on");
}

void TestGateIgnoreChangeTypes() {
    const auto a = MakeGraph({MakeNet("N", true, false, {"U1.1", "U1.2"})},
                             {MakePart("R1", "10k", "Device:R")});
    const auto b = MakeGraph({MakeNet("N", true, false, {"U1.1", "U1.2"})},
                             {MakePart("R1", "4k7", "Device:R")});
    Check(Diff(a, b, kDefault).summary.gate == GateResult::kFail,
          "gate: a value change FAILs by default");

    DiffConfig ignore_values;
    ignore_values.gate.ignore_change_types = {"ComponentModified"};
    const auto r = Diff(a, b, ignore_values);
    Check(r.summary.gate == GateResult::kPass,
          "gate: ignore_change_types excludes ComponentModified from gating");
    Check(r.summary.significant_count == 1,
          "gate: an ignored type is still reported, just not gated on");

    // Ignoring an unrelated type must not rescue the build.
    DiffConfig ignore_other;
    ignore_other.gate.ignore_change_types = {"NetMerged"};
    Check(Diff(a, b, ignore_other).summary.gate == GateResult::kFail,
          "gate: ignoring an unrelated type still FAILs");

    DiffConfig ignore_any;
    ignore_any.gate.fail_on = DiffConfig::Gate::FailOn::kAny;
    ignore_any.gate.ignore_change_types = {"ComponentModified"};
    Check(Diff(a, b, ignore_any).summary.gate == GateResult::kPass,
          "gate: ignore_change_types applies under fail_on=any too");
}

void TestGateSerializes() {
    const auto a = MakeGraph({MakeNet("GND", true, true, {"U1.1", "U2.1"}),
                              MakeNet("VCC", true, true, {"U3.1"})});
    const auto b = MakeGraph({MakeNet("GND", true, true, {"U1.1"}),
                              MakeNet("VCC", true, true, {"U3.1", "U2.1"})});
    const std::string json = SerializeDiffJson(Diff(a, b, kDefault));
    Check(json.find("\"gate\": \"FAIL\"") != std::string::npos,
          "gate verdict reaches the JSON");
}

}  // namespace

int main() {
    TestSignificanceTable();
    TestPositionIsNeverAChange();
    TestGateDefaultSignificant();
    TestGateCosmeticOnly();
    TestGateNever();
    TestGateIgnoreChangeTypes();
    TestGateSerializes();

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "significance + gate: ok\n";
    return 0;
}
