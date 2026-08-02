// T1.1: DiffResult + every Change type from 02_DATA_MODEL.md §3 construct,
// classify and serialize deterministically.
//
// argv[1] = path to the golden JSON (tests/golden/diff_all_change_types.json).
// On mismatch the actual output is written next to it as *.actual for diffing.

#include "netdiff/diff.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using netdiff::Change;
using netdiff::ChangeSortKey;
using netdiff::ChangeType;
using netdiff::Component;
using netdiff::ComponentAdded;
using netdiff::ComponentModified;
using netdiff::ComponentRemoved;
using netdiff::DiffResult;
using netdiff::FieldChange;
using netdiff::GateResult;
using netdiff::Net;
using netdiff::NetAdded;
using netdiff::NetMerged;
using netdiff::NetRemoved;
using netdiff::NetRenamed;
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

Component MakeComponent(const std::string& ref, const std::string& value,
                        const std::string& lib_id) {
    Component c;
    c.ref = ref;
    c.value = value;
    c.footprint = "Resistor_SMD:R_0402_1005Metric";
    c.lib_id = lib_id;
    c.sheet_path = "/power/";
    c.x = 12.7;
    c.y = 25.4;
    c.rotation = 90.0;
    Pin p1;
    p1.component_ref = ref;
    p1.number = "1";
    p1.name = "~";
    p1.unit = 1;
    Pin p2;
    p2.component_ref = ref;
    p2.number = "2";
    p2.name = "SDA";
    p2.unit = 1;
    c.pins = {p1, p2};
    return c;
}

Net MakeNet(const std::string& name, bool is_named, bool is_power,
            std::vector<std::string> pins, const std::string& net_id) {
    Net n;
    n.name = name;
    n.is_named = is_named;
    n.is_power = is_power;
    n.pins = std::move(pins);
    n.sheet_scope = is_power ? "global" : "/power/";
    n.net_id = net_id;
    return n;
}

// One change of every type in 02_DATA_MODEL.md §3.2, deliberately out of order
// so SortChanges has real work to do.
std::vector<Change> MakeAllChangeTypes() {
    std::vector<Change> changes;

    {
        Change c;
        c.payload = PinConnectionChanged{"U3.7", "GND", "+3V3"};
        c.message = "U3.7 moved from GND to +3V3";
        changes.push_back(std::move(c));
    }
    {
        Change c;
        c.payload = NetRenamed{"Net-(R4-Pad2)", "Net-(R4-Pad3)",
                               MakeNet("Net-(R4-Pad3)", false, false, {"R4.2", "R5.1"}, "b2")};
        c.significance = Significance::kCosmetic;
        c.message = "Net renamed: Net-(R4-Pad2) -> Net-(R4-Pad3)";
        changes.push_back(std::move(c));
    }
    {
        Change c;
        c.payload = ComponentRemoved{MakeComponent("R17", "10k", "Device:R")};
        c.message = "R17 (10k) removed";
        changes.push_back(std::move(c));
    }
    {
        Change c;
        c.payload = NetMerged{{"+3V3", "+5V"}, "+5V", {"C12.1", "U3.14"}};
        c.critical = true;
        c.message = "Nets +3V3 and +5V merged into +5V (via C12.1)";
        changes.push_back(std::move(c));
    }
    {
        // Second PinConnectionChanged: same type, different key — proves the
        // stable key orders within a type (R1.2 before U3.7).
        Change c;
        c.payload = PinConnectionChanged{"R1.2", "SCL", netdiff::kUnconnectedNetName};
        c.message = "R1.2 moved from SCL to unconnected";
        changes.push_back(std::move(c));
    }
    {
        Change c;
        c.payload = ComponentAdded{MakeComponent("C9", "100n", "Device:C")};
        c.message = "C9 (100n) added";
        changes.push_back(std::move(c));
    }
    {
        Change c;
        c.payload = NetSplit{"I2C_SDA", {"I2C_SDA", "I2C_SDA_2"}, {"U3.7", "R4.1"}};
        c.message = "Net I2C_SDA split into I2C_SDA and I2C_SDA_2";
        changes.push_back(std::move(c));
    }
    {
        Change c;
        c.payload = NetRemoved{MakeNet("VBUS", true, true, {"J1.1", "U7.3"}, "a1")};
        c.message = "Net VBUS removed";
        changes.push_back(std::move(c));
    }
    {
        Change c;
        c.payload = ComponentModified{"R5", {FieldChange{"value", "10k", "4k7"},
                                             FieldChange{"footprint", "R_0402", "R_0603"}}};
        c.message = "R5 value 10k -> 4k7, footprint R_0402 -> R_0603";
        changes.push_back(std::move(c));
    }
    {
        Change c;
        c.payload = NetAdded{MakeNet("I2C_SCL", true, false, {"U3.8", "R6.2"}, "c3")};
        c.message = "Net I2C_SCL added";
        changes.push_back(std::move(c));
    }

    return changes;
}

DiffResult MakeResult(std::vector<Change> changes) {
    DiffResult r;
    r.before_ref = "HEAD~1";
    r.after_ref = "working-tree";
    r.changes = std::move(changes);
    netdiff::SortChanges(r.changes);
    netdiff::RecomputeCounts(r);
    r.summary.gate = GateResult::kFail;
    return r;
}

std::string ReadFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::string();
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: test_diff_types <golden.json>\n";
        return 2;
    }

    // 1. Every payload alternative reports the matching ChangeType — the tag is
    //    derived from the variant, so this pins the declaration order.
    {
        const std::vector<std::pair<Change, ChangeType>> expected = {
            {Change{ComponentAdded{}, Significance::kSignificant, false, ""},
             ChangeType::kComponentAdded},
            {Change{ComponentRemoved{}, Significance::kSignificant, false, ""},
             ChangeType::kComponentRemoved},
            {Change{ComponentModified{}, Significance::kSignificant, false, ""},
             ChangeType::kComponentModified},
            {Change{NetAdded{}, Significance::kSignificant, false, ""},
             ChangeType::kNetAdded},
            {Change{NetRemoved{}, Significance::kSignificant, false, ""},
             ChangeType::kNetRemoved},
            {Change{NetRenamed{}, Significance::kSignificant, false, ""},
             ChangeType::kNetRenamed},
            {Change{NetMerged{}, Significance::kSignificant, false, ""},
             ChangeType::kNetMerged},
            {Change{NetSplit{}, Significance::kSignificant, false, ""},
             ChangeType::kNetSplit},
            {Change{PinConnectionChanged{}, Significance::kSignificant, false, ""},
             ChangeType::kPinConnectionChanged},
        };
        for (const auto& kv : expected) {
            Check(kv.first.type() == kv.second,
                  std::string("payload maps to ") + netdiff::ToString(kv.second));
        }
        Check(expected.size() == 9, "all nine change types covered");
    }

    const DiffResult result = MakeResult(MakeAllChangeTypes());

    // 2. Counts and gate.
    Check(result.summary.significant_count == 9, "significant_count == 9");
    Check(result.summary.cosmetic_count == 1, "cosmetic_count == 1 (auto-named rename)");
    Check(result.summary.by_type.size() == 9, "by_type lists nine types");
    Check(result.summary.by_type.at("PinConnectionChanged") == 2,
          "by_type counts both PinConnectionChanged");
    Check(result.summary.by_type.count("NetRenamed") == 1, "by_type includes NetRenamed");

    // 3. Sort order: significance descending, then ChangeType, then stable key.
    {
        const std::vector<ChangeType> want = {
            ChangeType::kComponentAdded,       ChangeType::kComponentRemoved,
            ChangeType::kComponentModified,    ChangeType::kNetAdded,
            ChangeType::kNetRemoved,           ChangeType::kNetMerged,
            ChangeType::kNetSplit,             ChangeType::kPinConnectionChanged,
            ChangeType::kPinConnectionChanged, ChangeType::kNetRenamed,
        };
        Check(result.changes.size() == want.size(), "change count preserved by sort");
        for (size_t i = 0; i < want.size() && i < result.changes.size(); ++i) {
            Check(result.changes[i].type() == want[i],
                  "sorted position " + std::to_string(i) + " is " +
                      netdiff::ToString(want[i]) + " (got " +
                      netdiff::ToString(result.changes[i].type()) + ")");
        }
        if (result.changes.size() == want.size()) {
            Check(ChangeSortKey(result.changes[7]) == "R1.2" &&
                      ChangeSortKey(result.changes[8]) == "U3.7",
                  "same-type changes ordered by stable key");
            Check(result.changes.back().significance == Significance::kCosmetic,
                  "cosmetic change sorts last");
        }
    }

    const std::string json = netdiff::SerializeDiffJson(result);

    // 4. Determinism: repeated serialization is byte-identical (06 §3.3).
    for (int i = 0; i < 100; ++i) {
        if (netdiff::SerializeDiffJson(result) != json) {
            Check(false, "serialization is byte-identical across 100 runs");
            break;
        }
    }

    // 5. Order-independence: the same changes in any input order produce the
    //    same bytes once sorted, so nothing depends on construction order.
    {
        std::vector<Change> shuffled = MakeAllChangeTypes();
        std::reverse(shuffled.begin(), shuffled.end());
        const DiffResult other = MakeResult(std::move(shuffled));
        Check(netdiff::SerializeDiffJson(other) == json,
              "reversed input order yields identical JSON");

        std::vector<Change> rotated = MakeAllChangeTypes();
        std::rotate(rotated.begin(), rotated.begin() + 4, rotated.end());
        const DiffResult third = MakeResult(std::move(rotated));
        Check(netdiff::SerializeDiffJson(third) == json,
              "rotated input order yields identical JSON");
    }

    // 6. Golden contract: exact bytes, including key order at every level.
    {
        const std::string golden_path = argv[1];
        const std::string golden = ReadFile(golden_path);
        if (golden != json) {
            const std::string actual_path = golden_path + ".actual";
            std::ofstream out(actual_path, std::ios::binary);
            out << json;
            out.close();
            Check(false, "golden mismatch; wrote " + actual_path);
        }
    }

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "diff types: ok\n";
    return 0;
}
