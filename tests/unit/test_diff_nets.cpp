// T1.3: net matching tiers 1-3 — 03_DIFF_ALGORITHM.md §4.
//
// argv[1..] = corpus entry schematics used for the self-diff check.

#include "netdiff/diff.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

using netdiff::Change;
using netdiff::ChangeType;
using netdiff::ConnectivityGraph;
using netdiff::Diff;
using netdiff::DiffConfig;
using netdiff::DiffResult;
using netdiff::Net;
using netdiff::NetRenamed;
using netdiff::Significance;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << "\n";
        ++g_failures;
    }
}

// net_id is "a stable hash of the sorted pin-set" (02 §2.3); joining the sorted
// pins is such a function and keeps the fixtures readable.
Net MakeNet(const std::string& name, bool is_named, std::vector<std::string> pins) {
    Net n;
    n.name = name;
    n.is_named = is_named;
    std::sort(pins.begin(), pins.end());
    n.pins = std::move(pins);
    for (const auto& p : n.pins) {
        n.net_id += p;
        n.net_id += "|";
    }
    return n;
}

ConnectivityGraph MakeGraph(std::vector<Net> nets) {
    ConnectivityGraph g;
    g.source.revision = "rev";
    g.nets = std::move(nets);
    g.stats.net_count = static_cast<int>(g.nets.size());
    return g;
}

std::vector<ChangeType> TypesOf(const DiffResult& result) {
    std::vector<ChangeType> types;
    for (const auto& c : result.changes) {
        types.push_back(c.type());
    }
    return types;
}

const DiffConfig kConfig{};

// ---- Tier 1: exact name ----------------------------------------------------

void TestSameNameSamePins() {
    const auto g = MakeGraph({MakeNet("GND", true, {"R1.1", "U1.8"})});
    Check(Diff(g, g, kConfig).changes.empty(), "tier1: identical named net is silent");
}

// Matched by name but the pin-set moved: step 4 reports the pin changes, so
// tier 3 must not fall through to remove+add here.
void TestSameNameDifferentPins() {
    const auto a = MakeGraph({MakeNet("GND", true, {"R1.1", "U1.8"})});
    const auto b = MakeGraph({MakeNet("GND", true, {"R1.1", "U1.8", "C3.2"})});
    const auto result = Diff(a, b, kConfig);
    Check(result.changes.empty(),
          "tier1: same name with changed pins emits no net add/remove/rename");
}

// A label repeated on several sheets is not a unique key (vme-wren: TERM_EN0
// four times). Each occurrence must pair with its counterpart, not collapse.
void TestDuplicateNames() {
    const auto a = MakeGraph({MakeNet("B", true, {"U1.1", "U1.2"}),
                              MakeNet("B", true, {"U2.1", "U2.2"})});
    const auto b = MakeGraph({MakeNet("B", true, {"U1.1", "U1.2"}),
                              MakeNet("B", true, {"U2.1", "U2.2"})});
    const auto result = Diff(a, b, kConfig);
    Check(result.changes.empty(), "tier1: repeated label name pairs off cleanly");
}

// ---- Tier 2: identical pin-set, different name -----------------------------

void TestRenameSignificant() {
    const auto a = MakeGraph({MakeNet("I2C_SDA", true, {"U3.7", "R4.2"})});
    const auto b = MakeGraph({MakeNet("SDA_BUS", true, {"U3.7", "R4.2"})});
    const auto result = Diff(a, b, kConfig);
    Check(TypesOf(result) == std::vector<ChangeType>{ChangeType::kNetRenamed},
          "tier2: exactly one NetRenamed");
    if (result.changes.size() == 1) {
        const auto* payload = std::get_if<NetRenamed>(&result.changes[0].payload);
        Check(payload != nullptr && payload->before_name == "I2C_SDA" &&
                  payload->after_name == "SDA_BUS",
              "tier2: before/after names on the payload");
        Check(payload != nullptr && payload->net.pins.size() == 2,
              "tier2: payload carries the net");
        Check(result.changes[0].significance == Significance::kSignificant,
              "tier2: user-named rename is SIGNIFICANT");
        Check(result.changes[0].message == "Net renamed: I2C_SDA -> SDA_BUS",
              "tier2: message");
    }
}

// Both names auto-generated → nobody is reading them, so COSMETIC (03 §4).
void TestRenameCosmetic() {
    const auto a = MakeGraph({MakeNet("Net-(R4-Pad2)", false, {"R4.2", "R5.1"})});
    const auto b = MakeGraph({MakeNet("Net-(R5-Pad1)", false, {"R4.2", "R5.1"})});
    const auto result = Diff(a, b, kConfig);
    Check(TypesOf(result) == std::vector<ChangeType>{ChangeType::kNetRenamed},
          "tier2: auto-name change is a NetRenamed");
    if (result.changes.size() == 1) {
        Check(result.changes[0].significance == Significance::kCosmetic,
              "tier2: auto-generated rename is COSMETIC");
    }
    Check(result.summary.significant_count == 0, "tier2: cosmetic rename does not gate");
    Check(result.summary.cosmetic_count == 1, "tier2: counted as cosmetic");
}

void TestRenameAutoToUserIsSignificant() {
    const auto a = MakeGraph({MakeNet("Net-(R4-Pad2)", false, {"R4.2", "R5.1"})});
    const auto b = MakeGraph({MakeNet("I2C_SDA", true, {"R4.2", "R5.1"})});
    const auto result = Diff(a, b, kConfig);
    Check(result.changes.size() == 1 &&
              result.changes[0].significance == Significance::kSignificant,
          "tier2: naming a previously auto-named net is SIGNIFICANT");
    if (result.changes.size() == 1) {
        Check(result.changes[0].message == "Net renamed: Net-(R4-Pad2) -> I2C_SDA",
              "tier2: message matches the spec example");
    }
}

// ---- Tier 3: fuzzy ---------------------------------------------------------

// [AC] An unnamed net that gained a pin is the SAME net, not remove + add.
void TestUnnamedNetGainedPin() {
    const auto a = MakeGraph({MakeNet("", false, {"R1.2", "U1.3"})});
    const auto b = MakeGraph({MakeNet("", false, {"R1.2", "U1.3", "C7.1"})});
    const auto result = Diff(a, b, kConfig);
    Check(result.changes.empty(),
          "tier3: unnamed net that gained a pin is matched, not removed+added");
    Check(result.summary.by_type.count("NetAdded") == 0, "tier3: no NetAdded");
    Check(result.summary.by_type.count("NetRemoved") == 0, "tier3: no NetRemoved");
}

// 2/3 = 0.667 passes the 0.6 default; 1/5 = 0.2 does not.
void TestFuzzyBelowThresholdSplitsApart() {
    const auto a = MakeGraph({MakeNet("", false, {"R1.2", "U1.3"})});
    const auto b = MakeGraph({MakeNet("", false, {"R1.2", "X1.1", "X2.1", "X3.1"})});
    const auto result = Diff(a, b, kConfig);
    Check(TypesOf(result) == std::vector<ChangeType>{ChangeType::kNetAdded,
                                                     ChangeType::kNetRemoved},
          "tier3: below threshold becomes NetAdded + NetRemoved");
}

void TestThresholdIsConfigurable() {
    const auto a = MakeGraph({MakeNet("", false, {"R1.2", "U1.3"})});
    const auto b = MakeGraph({MakeNet("", false, {"R1.2", "X1.1", "X2.1", "X3.1"})});
    DiffConfig loose;
    loose.unnamed_net_matching.jaccard_threshold = 0.2;
    Check(Diff(a, b, loose).changes.empty(),
          "tier3: lowering the threshold matches the same pair");
}

void TestNetAddedAndRemoved() {
    const auto a = MakeGraph({MakeNet("VBUS", true, {"J1.1", "U7.3"})});
    const auto b = MakeGraph({MakeNet("I2C_SCL", true, {"U3.8", "R6.2"})});
    const auto result = Diff(a, b, kConfig);
    Check(TypesOf(result) == std::vector<ChangeType>{ChangeType::kNetAdded,
                                                     ChangeType::kNetRemoved},
          "disjoint nets: one added, one removed");
    for (const auto& c : result.changes) {
        Check(c.significance == Significance::kSignificant, "add/remove: SIGNIFICANT");
    }
    Check(result.changes[0].message == "Net I2C_SCL added", "added: message");
    Check(result.changes[1].message == "Net VBUS removed", "removed: message");
}

void TestUnnamedNetMessage() {
    const auto a = MakeGraph({MakeNet("", false, {"D1.1", "D2.2"})});
    const auto b = MakeGraph({MakeNet("KEEP", true, {"Z9.1", "Z9.2"})});
    const auto result = Diff(a, b, kConfig);
    bool found = false;
    for (const auto& c : result.changes) {
        if (c.message == "Unnamed net (D1.1, D2.2) removed") {
            found = true;
        }
    }
    Check(found, "unnamed net is described by its pins in the message");
}

// ---- Determinism -----------------------------------------------------------

// Two equally-similar candidates (both 3/4) must resolve the same way every
// time and under any input ordering — tie-break is lexicographic net_id.
void TestTieBreakDeterminism() {
    std::vector<Net> a_nets = {MakeNet("", false, {"P1.1", "P2.1", "P3.1"})};
    std::vector<Net> b_nets = {MakeNet("", false, {"P1.1", "P2.1", "P3.1", "P4.1"}),
                               MakeNet("", false, {"P1.1", "P2.1", "P3.1", "P5.1"})};

    const std::string baseline =
        SerializeDiffJson(Diff(MakeGraph(a_nets), MakeGraph(b_nets), kConfig));
    for (int i = 0; i < 50; ++i) {
        if (SerializeDiffJson(Diff(MakeGraph(a_nets), MakeGraph(b_nets), kConfig)) !=
            baseline) {
            Check(false, "tie: repeated runs agree");
            break;
        }
    }
    std::vector<Net> b_rev(b_nets.rbegin(), b_nets.rend());
    Check(SerializeDiffJson(Diff(MakeGraph(a_nets), MakeGraph(b_rev), kConfig)) == baseline,
          "tie: reversing the candidates does not change the winner");

    // Exactly one of the two B-nets is left over as NetAdded.
    const auto result = Diff(MakeGraph(a_nets), MakeGraph(b_nets), kConfig);
    Check(TypesOf(result) == std::vector<ChangeType>{ChangeType::kNetAdded},
          "tie: one candidate matched, the other reported added");
}

void TestOrderIndependence() {
    std::vector<Net> a_nets = {MakeNet("GND", true, {"R1.1", "U1.8"}),
                               MakeNet("I2C_SDA", true, {"U3.7", "R4.2"}),
                               MakeNet("", false, {"R9.1", "R9.2"})};
    std::vector<Net> b_nets = {MakeNet("GND", true, {"R1.1", "U1.8", "C3.2"}),
                               MakeNet("SDA_BUS", true, {"U3.7", "R4.2"}),
                               MakeNet("VCC", true, {"U5.1"})};
    const std::string baseline =
        SerializeDiffJson(Diff(MakeGraph(a_nets), MakeGraph(b_nets), kConfig));

    std::vector<Net> a_rev(a_nets.rbegin(), a_nets.rend());
    std::vector<Net> b_rev(b_nets.rbegin(), b_nets.rend());
    Check(SerializeDiffJson(Diff(MakeGraph(a_rev), MakeGraph(b_rev), kConfig)) == baseline,
          "nets: reversed input order yields identical JSON");

    std::rotate(a_nets.begin(), a_nets.begin() + 1, a_nets.end());
    std::rotate(b_nets.begin(), b_nets.begin() + 2, b_nets.end());
    Check(SerializeDiffJson(Diff(MakeGraph(a_nets), MakeGraph(b_nets), kConfig)) == baseline,
          "nets: rotated input order yields identical JSON");
}

// A real project against itself must stay silent now that nets are matched too.
void TestCorpusSelfDiff(const std::string& entry) {
    netdiff::ProjectInput input;
    input.entry_file = entry;
    const ConnectivityGraph graph = netdiff::BuildGraph(input);
    const auto result = Diff(graph, graph, kConfig);
    if (!result.changes.empty()) {
        std::cerr << "  first offending change: " << result.changes[0].message << "\n";
    }
    Check(result.changes.empty(),
          "self-diff of " + entry + " emits no changes (got " +
              std::to_string(result.changes.size()) + ")");
}

}  // namespace

int main(int argc, char** argv) {
    TestSameNameSamePins();
    TestSameNameDifferentPins();
    TestDuplicateNames();
    TestRenameSignificant();
    TestRenameCosmetic();
    TestRenameAutoToUserIsSignificant();
    TestUnnamedNetGainedPin();
    TestFuzzyBelowThresholdSplitsApart();
    TestThresholdIsConfigurable();
    TestNetAddedAndRemoved();
    TestUnnamedNetMessage();
    TestTieBreakDeterminism();
    TestOrderIndependence();

    for (int i = 1; i < argc; ++i) {
        TestCorpusSelfDiff(argv[i]);
    }

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "net matching: ok\n";
    return 0;
}
