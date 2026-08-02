// T1.4: pin-level connectivity changes and merge/split detection —
// 03_DIFF_ALGORITHM.md §5-6.
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
using netdiff::ConnectivityGraph;
using netdiff::Diff;
using netdiff::DiffConfig;
using netdiff::DiffResult;
using netdiff::Net;
using netdiff::NetMerged;
using netdiff::NetSplit;
using netdiff::Pin;
using netdiff::PinConnectionChanged;
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

// Components are synthesised from the pins the nets mention, so every fixture
// automatically has the matched components that step 4 requires. `loose_pins`
// adds pins that exist on a component but sit on no net — a real graph lists
// an unconnected pin on its Component and in no Net.
ConnectivityGraph MakeGraph(std::vector<Net> nets,
                            const std::vector<std::string>& loose_pins = {}) {
    ConnectivityGraph g;
    g.source.revision = "rev";
    g.nets = std::move(nets);

    std::vector<std::pair<std::string, std::string>> refs;
    auto add_pin = [&refs](const std::string& pin_id) {
        const auto dot = pin_id.rfind('.');
        refs.emplace_back(pin_id.substr(0, dot), pin_id.substr(dot + 1));
    };
    for (const auto& net : g.nets) {
        for (const auto& pin_id : net.pins) {
            add_pin(pin_id);
        }
    }
    for (const auto& pin_id : loose_pins) {
        add_pin(pin_id);
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
    return g;
}

std::vector<ChangeType> TypesOf(const DiffResult& result) {
    std::vector<ChangeType> types;
    for (const auto& c : result.changes) {
        types.push_back(c.type());
    }
    return types;
}

int CountOf(const DiffResult& result, const std::string& type) {
    const auto it = result.summary.by_type.find(type);
    return it == result.summary.by_type.end() ? 0 : it->second;
}

const DiffConfig kConfig{};

// [AC] A pin moved GND -> +3V3 yields exactly one PinConnectionChanged.
void TestPinMovedBetweenRails() {
    const auto a = MakeGraph({MakeNet("GND", true, true, {"U3.7", "R1.1"}),
                              MakeNet("+3V3", true, true, {"C1.1"})});
    const auto b = MakeGraph({MakeNet("GND", true, true, {"R1.1"}),
                              MakeNet("+3V3", true, true, {"C1.1", "U3.7"})});
    const auto result = Diff(a, b, kConfig);

    Check(CountOf(result, "PinConnectionChanged") == 1,
          "exactly one PinConnectionChanged (got " +
              std::to_string(CountOf(result, "PinConnectionChanged")) + ")");
    Check(result.changes.size() == 1, "a pin move emits nothing else");
    if (result.changes.size() == 1) {
        const auto* payload = std::get_if<PinConnectionChanged>(&result.changes[0].payload);
        Check(payload != nullptr && payload->pin == "U3.7" &&
                  payload->before_net == "GND" && payload->after_net == "+3V3",
              "payload names the pin and both nets");
        Check(result.changes[0].significance == Significance::kSignificant,
              "PinConnectionChanged is always SIGNIFICANT");
        Check(result.changes[0].message == "U3.7 moved from GND to +3V3",
              "message matches the spec example");
    }
}

// A rename must NOT read as every pin being re-tied: identity comes from the
// step-3 match, not the net's name.
void TestRenameDoesNotMovePins() {
    const auto a = MakeGraph({MakeNet("I2C_SDA", true, false, {"U3.7", "R4.2", "C1.1"})});
    const auto b = MakeGraph({MakeNet("SDA_BUS", true, false, {"U3.7", "R4.2", "C1.1"})});
    const auto result = Diff(a, b, kConfig);
    Check(CountOf(result, "PinConnectionChanged") == 0,
          "a renamed net does not emit PinConnectionChanged for its pins");
    Check(TypesOf(result) == std::vector<ChangeType>{ChangeType::kNetRenamed},
          "a rename emits only NetRenamed");
}

void TestPinBecameUnconnected() {
    const auto a = MakeGraph({MakeNet("SIG", true, false, {"U1.1", "U2.1"}),
                              MakeNet("KEEP", true, false, {"U9.1", "U9.2"})});
    // U2 survives; only its wire is gone, so U2.1 is now on no net at all.
    const auto b = MakeGraph({MakeNet("SIG", true, false, {"U1.1"}),
                              MakeNet("KEEP", true, false, {"U9.1", "U9.2"})},
                             {"U2.1"});
    const auto result = Diff(a, b, kConfig);
    bool found = false;
    for (const auto& c : result.changes) {
        const auto* payload = std::get_if<PinConnectionChanged>(&c.payload);
        if (payload != nullptr && payload->pin == "U2.1" &&
            payload->before_net == "SIG" && payload->after_net == "unconnected") {
            found = true;
        }
    }
    Check(found, "a pin that lost its net reports after_net = unconnected");
}

// [AC] Two rails shorted together yields NetMerged flagged critical.
void TestShortedRailsMerge() {
    const auto a = MakeGraph({MakeNet("+5V", true, true, {"U1.1", "C1.1"}),
                              MakeNet("+3V3", true, true, {"U2.1", "C2.1"})});
    const auto b = MakeGraph({MakeNet("+5V", true, true, {"U1.1", "C1.1", "U2.1", "C2.1"})});
    const auto result = Diff(a, b, kConfig);

    Check(CountOf(result, "NetMerged") == 1, "shorted rails produce one NetMerged");
    const Change* merged = nullptr;
    for (const auto& c : result.changes) {
        if (c.type() == ChangeType::kNetMerged) {
            merged = &c;
        }
    }
    if (merged != nullptr) {
        Check(merged->critical, "a power-net merge is flagged critical");
        Check(merged->significance == Significance::kSignificant, "NetMerged is SIGNIFICANT");
        const auto* payload = std::get_if<NetMerged>(&merged->payload);
        Check(payload != nullptr &&
                  payload->before_nets == std::vector<std::string>{"+3V3", "+5V"},
              "NetMerged lists both before-nets, sorted");
        Check(payload != nullptr && payload->after_net == "+5V", "NetMerged names the survivor");
        Check(payload != nullptr &&
                  payload->pins_involved == std::vector<std::string>{"C2.1", "U2.1"},
              "NetMerged attributes the pins that moved");
        Check(merged->message == "Nets +3V3 and +5V merged into +5V (via C2.1, U2.1)",
              "NetMerged message, got: " + merged->message);
    }
}

// Non-power nets merging is significant but not critical.
void TestSignalMergeIsNotCritical() {
    const auto a = MakeGraph({MakeNet("SIG_A", true, false, {"U1.1", "C1.1"}),
                              MakeNet("SIG_B", true, false, {"U2.1", "C2.1"})});
    const auto b = MakeGraph({MakeNet("SIG_A", true, false, {"U1.1", "C1.1", "U2.1", "C2.1"})});
    const auto result = Diff(a, b, kConfig);
    for (const auto& c : result.changes) {
        if (c.type() == ChangeType::kNetMerged) {
            Check(!c.critical, "a signal-net merge is not critical");
        }
    }
    Check(CountOf(result, "NetMerged") == 1, "signal merge still detected");
}

// [AC] A net broken into two yields NetSplit.
void TestNetSplit() {
    const auto a = MakeGraph({MakeNet("SIG", true, false, {"U1.1", "U2.1", "U3.1"})});
    const auto b = MakeGraph({MakeNet("SIG", true, false, {"U1.1", "U2.1"}),
                              MakeNet("SIG2", true, false, {"U3.1"})});
    const auto result = Diff(a, b, kConfig);

    Check(CountOf(result, "NetSplit") == 1, "a broken net produces one NetSplit");
    const Change* split = nullptr;
    for (const auto& c : result.changes) {
        if (c.type() == ChangeType::kNetSplit) {
            split = &c;
        }
    }
    if (split != nullptr) {
        Check(split->significance == Significance::kSignificant, "NetSplit is SIGNIFICANT");
        const auto* payload = std::get_if<NetSplit>(&split->payload);
        Check(payload != nullptr && payload->before_net == "SIG", "NetSplit names the origin");
        Check(payload != nullptr &&
                  payload->after_nets == std::vector<std::string>{"SIG", "SIG2"},
              "NetSplit lists the resulting nets, sorted");
        Check(payload != nullptr &&
                  payload->pins_involved == std::vector<std::string>{"U3.1"},
              "NetSplit attributes the pin that left");
    }
}

// A pin swap between two rails is neither a merge nor a split — both sides keep
// their identity, so only the pin-level change should be reported.
void TestSwapIsNotMergeOrSplit() {
    const auto a = MakeGraph({MakeNet("GND", true, true, {"U3.7", "R1.1"}),
                              MakeNet("+3V3", true, true, {"C1.1"})});
    const auto b = MakeGraph({MakeNet("GND", true, true, {"R1.1"}),
                              MakeNet("+3V3", true, true, {"C1.1", "U3.7"})});
    const auto result = Diff(a, b, kConfig);
    Check(CountOf(result, "NetMerged") == 0, "a pin move is not reported as a merge");
    Check(CountOf(result, "NetSplit") == 0, "a pin move is not reported as a split");
}

// Pins arriving or leaving with their component are covered by
// ComponentAdded/Removed, not by phantom pin moves.
void TestRemovedComponentPinsAreNotPinChanges() {
    const auto a = MakeGraph({MakeNet("SIG", true, false, {"U1.1", "R9.1"})});
    const auto b = MakeGraph({MakeNet("SIG", true, false, {"U1.1"})});
    const auto result = Diff(a, b, kConfig);
    Check(CountOf(result, "ComponentRemoved") == 1, "the vanished component is reported");
    Check(CountOf(result, "PinConnectionChanged") == 0,
          "no PinConnectionChanged for a pin whose component was removed");
}

void TestOrderIndependence() {
    std::vector<Net> a_nets = {MakeNet("+5V", true, true, {"U1.1", "C1.1"}),
                               MakeNet("+3V3", true, true, {"U2.1", "C2.1"}),
                               MakeNet("SIG", true, false, {"U4.1", "U5.1", "U6.1"})};
    std::vector<Net> b_nets = {MakeNet("+5V", true, true, {"U1.1", "C1.1", "U2.1", "C2.1"}),
                               MakeNet("SIG", true, false, {"U4.1", "U5.1"}),
                               MakeNet("SIG2", true, false, {"U6.1"})};
    const std::string baseline =
        SerializeDiffJson(Diff(MakeGraph(a_nets), MakeGraph(b_nets), kConfig));

    std::vector<Net> a_rev(a_nets.rbegin(), a_nets.rend());
    std::vector<Net> b_rev(b_nets.rbegin(), b_nets.rend());
    Check(SerializeDiffJson(Diff(MakeGraph(a_rev), MakeGraph(b_rev), kConfig)) == baseline,
          "merge+split: reversed input order yields identical JSON");
    for (int i = 0; i < 25; ++i) {
        if (SerializeDiffJson(Diff(MakeGraph(a_nets), MakeGraph(b_nets), kConfig)) != baseline) {
            Check(false, "merge+split: repeated runs agree");
            break;
        }
    }
    // Both classifications fire in one diff.
    const auto result = Diff(MakeGraph(a_nets), MakeGraph(b_nets), kConfig);
    Check(CountOf(result, "NetMerged") == 1 && CountOf(result, "NetSplit") == 1,
          "a merge and a split are detected independently in one diff");
}

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
    TestPinMovedBetweenRails();
    TestRenameDoesNotMovePins();
    TestPinBecameUnconnected();
    TestShortedRailsMerge();
    TestSignalMergeIsNotCritical();
    TestNetSplit();
    TestSwapIsNotMergeOrSplit();
    TestRemovedComponentPinsAreNotPinChanges();
    TestOrderIndependence();

    for (int i = 1; i < argc; ++i) {
        TestCorpusSelfDiff(argv[i]);
    }

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "pin changes + merge/split: ok\n";
    return 0;
}
