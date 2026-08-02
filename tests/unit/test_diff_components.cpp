// T1.2: component matching — 03_DIFF_ALGORITHM.md §3.
//
// argv[1..] = corpus entry schematics used for the self-diff check.

#include "netdiff/diff.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

using netdiff::Change;
using netdiff::ChangeType;
using netdiff::Component;
using netdiff::ComponentAdded;
using netdiff::ComponentModified;
using netdiff::ComponentRemoved;
using netdiff::ConnectivityGraph;
using netdiff::Diff;
using netdiff::DiffConfig;
using netdiff::DiffResult;
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

Component MakeComponent(const std::string& ref, const std::string& value,
                        const std::string& lib_id, const std::string& footprint = "FP:A",
                        const std::string& sheet_path = "/") {
    Component c;
    c.ref = ref;
    c.value = value;
    c.lib_id = lib_id;
    c.footprint = footprint;
    c.sheet_path = sheet_path;
    Pin p;
    p.component_ref = ref;
    p.number = "1";
    p.name = "~";
    c.pins.push_back(p);
    return c;
}

ConnectivityGraph MakeGraph(std::vector<Component> components) {
    ConnectivityGraph g;
    g.source.revision = "rev";
    g.components = std::move(components);
    g.stats.component_count = static_cast<int>(g.components.size());
    return g;
}

std::vector<ChangeType> TypesOf(const DiffResult& result) {
    std::vector<ChangeType> types;
    for (const auto& c : result.changes) {
        types.push_back(c.type());
    }
    return types;
}

bool AllSignificant(const DiffResult& result) {
    for (const auto& c : result.changes) {
        if (c.significance != Significance::kSignificant) {
            return false;
        }
    }
    return true;
}

const DiffConfig kConfig{};

void TestUnchanged() {
    const auto a = MakeGraph({MakeComponent("R1", "10k", "Device:R"),
                              MakeComponent("C1", "100n", "Device:C")});
    const auto result = Diff(a, a, kConfig);
    Check(result.changes.empty(), "identical graphs produce no changes");
    Check(result.summary.significant_count == 0, "identical graphs: significant_count == 0");
}

void TestAdded() {
    const auto a = MakeGraph({MakeComponent("R1", "10k", "Device:R")});
    const auto b = MakeGraph({MakeComponent("R1", "10k", "Device:R"),
                              MakeComponent("C9", "100n", "Device:C")});
    const auto result = Diff(a, b, kConfig);
    Check(TypesOf(result) == std::vector<ChangeType>{ChangeType::kComponentAdded},
          "added: exactly one ComponentAdded");
    if (result.changes.size() == 1) {
        const auto* payload = std::get_if<ComponentAdded>(&result.changes[0].payload);
        Check(payload != nullptr && payload->component.ref == "C9", "added: payload is C9");
        Check(result.changes[0].message == "C9 (100n) added", "added: message");
        Check(!result.changes[0].critical, "added: not critical");
    }
    Check(AllSignificant(result), "added: SIGNIFICANT");
}

void TestRemoved() {
    const auto a = MakeGraph({MakeComponent("R1", "10k", "Device:R"),
                              MakeComponent("R17", "10k", "Device:R")});
    const auto b = MakeGraph({MakeComponent("R1", "10k", "Device:R")});
    const auto result = Diff(a, b, kConfig);
    Check(TypesOf(result) == std::vector<ChangeType>{ChangeType::kComponentRemoved},
          "removed: exactly one ComponentRemoved");
    if (result.changes.size() == 1) {
        const auto* payload = std::get_if<ComponentRemoved>(&result.changes[0].payload);
        Check(payload != nullptr && payload->component.ref == "R17", "removed: payload is R17");
        Check(result.changes[0].message == "R17 (10k) removed", "removed: message");
    }
    Check(AllSignificant(result), "removed: SIGNIFICANT");
}

void TestModifiedValue() {
    const auto a = MakeGraph({MakeComponent("R5", "10k", "Device:R")});
    const auto b = MakeGraph({MakeComponent("R5", "4k7", "Device:R")});
    const auto result = Diff(a, b, kConfig);
    Check(TypesOf(result) == std::vector<ChangeType>{ChangeType::kComponentModified},
          "value change: one ComponentModified");
    if (result.changes.size() == 1) {
        const auto* payload = std::get_if<ComponentModified>(&result.changes[0].payload);
        Check(payload != nullptr && payload->ref == "R5", "modified: ref");
        Check(payload != nullptr && payload->changes.size() == 1 &&
                  payload->changes[0].field == "value" &&
                  payload->changes[0].before == "10k" && payload->changes[0].after == "4k7",
              "modified: one value FieldChange with before/after");
        Check(result.changes[0].message == "R5: value 10k -> 4k7", "modified: message");
    }
    Check(AllSignificant(result), "modified: SIGNIFICANT");
}

void TestModifiedFootprintAndOrder() {
    const auto a = MakeGraph({MakeComponent("R5", "10k", "Device:R", "R_0402")});
    const auto b = MakeGraph({MakeComponent("R5", "4k7", "Device:R", "R_0603")});
    const auto result = Diff(a, b, kConfig);
    Check(result.changes.size() == 1, "value+footprint: a single ComponentModified");
    if (result.changes.size() == 1) {
        const auto* payload = std::get_if<ComponentModified>(&result.changes[0].payload);
        Check(payload != nullptr && payload->changes.size() == 2,
              "value+footprint: two FieldChanges");
        if (payload != nullptr && payload->changes.size() == 2) {
            Check(payload->changes[0].field == "value" &&
                      payload->changes[1].field == "footprint",
                  "value+footprint: spec field order {value, footprint}");
        }
        Check(result.changes[0].message ==
                  "R5: value 10k -> 4k7, footprint R_0402 -> R_0603",
              "value+footprint: message");
    }
}

void TestFootprintOnly() {
    const auto a = MakeGraph({MakeComponent("R5", "10k", "Device:R", "R_0402")});
    const auto b = MakeGraph({MakeComponent("R5", "10k", "Device:R", "R_0603")});
    const auto result = Diff(a, b, kConfig);
    Check(TypesOf(result) == std::vector<ChangeType>{ChangeType::kComponentModified},
          "footprint change: one ComponentModified");
}

// The guard: a refdes reused for a different symbol is remove + add, never
// modified, and both halves are critical.
void TestRefdesReuseGuard() {
    const auto a = MakeGraph({MakeComponent("R5", "10k", "Device:R")});
    const auto b = MakeGraph({MakeComponent("R5", "100n", "Device:C")});
    const auto result = Diff(a, b, kConfig);

    Check(TypesOf(result) == std::vector<ChangeType>{ChangeType::kComponentAdded,
                                                     ChangeType::kComponentRemoved},
          "refdes reuse: ComponentAdded + ComponentRemoved, no ComponentModified");
    Check(result.summary.by_type.count("ComponentModified") == 0,
          "refdes reuse: never reported as ComponentModified");
    Check(result.changes.size() == 2 && result.changes[0].critical &&
              result.changes[1].critical,
          "refdes reuse: both halves flagged critical");
    Check(AllSignificant(result), "refdes reuse: SIGNIFICANT");
    for (const auto& change : result.changes) {
        Check(change.message.find("Device:R -> Device:C") != std::string::npos,
              "refdes reuse: message names both symbols");
    }
}

// lib_id change wins over a simultaneous value change — still remove + add.
void TestRefdesReuseBeatsFieldChanges() {
    const auto a = MakeGraph({MakeComponent("U1", "ESP32", "RF:ESP32", "FP:A")});
    const auto b = MakeGraph({MakeComponent("U1", "STM32", "MCU:STM32", "FP:B")});
    const auto result = Diff(a, b, kConfig);
    Check(TypesOf(result) == std::vector<ChangeType>{ChangeType::kComponentAdded,
                                                     ChangeType::kComponentRemoved},
          "refdes reuse with field changes: still remove + add only");
}

// ComponentId is the ref alone (02 §2.2), so relocating a part to another sheet
// is not an add/remove pair.
void TestSheetMoveIsNotAddRemove() {
    const auto a = MakeGraph({MakeComponent("IC14", "FPGA", "FPGA:X", "FP:A", "/fpga/banks/")});
    const auto b = MakeGraph({MakeComponent("IC14", "FPGA", "FPGA:X", "FP:A", "/fpga/mgts/")});
    const auto result = Diff(a, b, kConfig);
    Check(result.changes.empty(), "moving a part to another sheet emits no change");
}

void TestManyAtOnce() {
    const auto a = MakeGraph({MakeComponent("R1", "1k", "Device:R"),
                              MakeComponent("R2", "2k", "Device:R"),
                              MakeComponent("R3", "3k", "Device:R")});
    const auto b = MakeGraph({MakeComponent("R2", "2k2", "Device:R"),
                              MakeComponent("R3", "3k", "Device:C"),
                              MakeComponent("R4", "4k", "Device:R")});
    const auto result = Diff(a, b, kConfig);
    // R1 removed; R2 modified; R3 refdes-reused (remove+add); R4 added.
    Check(result.summary.by_type.at("ComponentAdded") == 2, "mixed: two adds (R4, R3-reuse)");
    Check(result.summary.by_type.at("ComponentRemoved") == 2, "mixed: two removes (R1, R3-reuse)");
    Check(result.summary.by_type.at("ComponentModified") == 1, "mixed: one modify (R2)");
    Check(result.summary.significant_count == 5, "mixed: five significant changes");
    Check(result.summary.cosmetic_count == 0, "mixed: no cosmetic changes");
}

// Nothing may depend on input ordering (03 §4, 06 §3.3).
void TestOrderIndependence() {
    std::vector<Component> a_components = {MakeComponent("R1", "1k", "Device:R"),
                                           MakeComponent("R2", "2k", "Device:R"),
                                           MakeComponent("R3", "3k", "Device:R")};
    std::vector<Component> b_components = {MakeComponent("R2", "2k2", "Device:R"),
                                           MakeComponent("R3", "3k", "Device:C"),
                                           MakeComponent("R4", "4k", "Device:R")};
    const std::string baseline =
        SerializeDiffJson(Diff(MakeGraph(a_components), MakeGraph(b_components), kConfig));

    std::vector<Component> a_rev(a_components.rbegin(), a_components.rend());
    std::vector<Component> b_rev(b_components.rbegin(), b_components.rend());
    Check(SerializeDiffJson(Diff(MakeGraph(a_rev), MakeGraph(b_rev), kConfig)) == baseline,
          "reversed component order yields identical JSON");

    std::rotate(a_components.begin(), a_components.begin() + 1, a_components.end());
    std::rotate(b_components.begin(), b_components.begin() + 2, b_components.end());
    Check(SerializeDiffJson(Diff(MakeGraph(a_components), MakeGraph(b_components), kConfig)) ==
              baseline,
          "rotated component order yields identical JSON");
}

// A real project diffed against itself must be silent — the T1.5 invariant in
// miniature, and the check that build_graph emits one record per ComponentId.
void TestCorpusSelfDiff(const std::string& entry) {
    netdiff::ProjectInput input;
    input.entry_file = entry;
    const ConnectivityGraph graph = netdiff::BuildGraph(input);
    const auto result = Diff(graph, graph, kConfig);
    Check(result.changes.empty(),
          "self-diff of " + entry + " emits no component changes (got " +
              std::to_string(result.changes.size()) + ")");

    std::vector<std::string> ids;
    for (const auto& c : graph.components) {
        ids.push_back(netdiff::MakeComponentId(c));
    }
    std::sort(ids.begin(), ids.end());
    Check(std::adjacent_find(ids.begin(), ids.end()) == ids.end(),
          "ComponentId is unique in " + entry);
}

}  // namespace

int main(int argc, char** argv) {
    TestUnchanged();
    TestAdded();
    TestRemoved();
    TestModifiedValue();
    TestModifiedFootprintAndOrder();
    TestFootprintOnly();
    TestRefdesReuseGuard();
    TestRefdesReuseBeatsFieldChanges();
    TestSheetMoveIsNotAddRemove();
    TestManyAtOnce();
    TestOrderIndependence();

    for (int i = 1; i < argc; ++i) {
        TestCorpusSelfDiff(argv[i]);
    }

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "component matching: ok\n";
    return 0;
}
