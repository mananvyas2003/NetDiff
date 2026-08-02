#include "netdiff/bus.hpp"
#include "netdiff/graph.hpp"
#include "netdiff/project_loader.hpp"

#include "interpreter.h"
#include "net_resolver.h"
#include "pin_mapper.h"
#include "pin_transform.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace netdiff {
namespace {

class StdoutSilence {
public:
    StdoutSilence()
        : old_out_(std::cout.rdbuf(sink_.rdbuf())),
          old_err_(std::cerr.rdbuf(sink_.rdbuf())) {}

    ~StdoutSilence() {
        std::cout.rdbuf(old_out_);
        std::cerr.rdbuf(old_err_);
    }

    StdoutSilence(const StdoutSilence&) = delete;
    StdoutSilence& operator=(const StdoutSilence&) = delete;

private:
    std::stringstream sink_;
    std::streambuf* old_out_;
    std::streambuf* old_err_;
};

std::string Basename(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    if (slash == std::string::npos) {
        return path;
    }
    return path.substr(slash + 1);
}

std::string StemName(const std::string& path) {
    std::string base = Basename(path);
    const auto dot = base.find_last_of('.');
    if (dot == std::string::npos) {
        return base;
    }
    return base.substr(0, dot);
}

bool LooksNamed(const std::string& name) {
    if (name.empty()) {
        return false;
    }
    if (name.rfind("Net-(", 0) == 0) {
        return false;
    }
    if (name.rfind("unconnected-", 0) == 0) {
        return false;
    }
    return true;
}

bool LooksPower(const std::string& name) {
    if (name.empty()) {
        return false;
    }
    if (name == "GND" || name == "GNDA" || name == "AGND" || name == "DGND") {
        return true;
    }
    if (name[0] == '+' || name[0] == '-') {
        return true;
    }
    if (name.rfind("VCC", 0) == 0 || name.rfind("VDD", 0) == 0 ||
        name.rfind("VSS", 0) == 0 || name.rfind("VBAT", 0) == 0) {
        return true;
    }
    return false;
}

std::string StableHashPinSet(const std::vector<std::string>& pins_sorted) {
    const uint64_t kOffset = 14695981039346656037ull;
    const uint64_t kPrime = 1099511628211ull;
    uint64_t h = kOffset;
    for (const auto& pin : pins_sorted) {
        for (unsigned char c : pin) {
            h ^= static_cast<uint64_t>(c);
            h *= kPrime;
        }
        h ^= static_cast<uint64_t>('\n');
        h *= kPrime;
    }
    std::ostringstream oss;
    oss << std::hex << h;
    return oss.str();
}

void ApplyInstanceReference(::Component& src, const std::string& uuid_path) {
    if (uuid_path.empty()) {
        return;
    }
    for (const auto& kv : src.instance_refs) {
        if (kv.first == uuid_path) {
            src.reference = kv.second;
            return;
        }
    }
}

void ApplyInstanceUnit(::Component& src, const std::string& uuid_path) {
    if (!uuid_path.empty()) {
        for (const auto& kv : src.instance_units) {
            if (kv.first == uuid_path) {
                src.unit = kv.second;
                break;
            }
        }
    }
    if (src.unit < 1) {
        src.unit = 1;
    }
    // Drop pins that belong to other units (keep unit 0 = common).
    src.pins.erase(
        std::remove_if(
            src.pins.begin(),
            src.pins.end(),
            [&](const ::Pin& pin) {
                return pin.unit != 0 && pin.unit != src.unit;
            }),
        src.pins.end());
}

struct SheetLocalNet {
    std::string sheet_path;
    std::string name;
    bool is_global = false;
    bool is_hierarchical = false;
    bool has_local_label = false;  // local_label (not sheet-pin / hier)
    bool has_hierarchical_label = false;  // hierarchical_label (not sheet pin)
    std::unordered_set<std::string> hierarchical_names;
    std::unordered_set<std::string> global_names;
    std::vector<std::string> pins;
};

struct PointKeyI {
    int x = 0;
    int y = 0;
    bool operator==(const PointKeyI& o) const { return x == o.x && y == o.y; }
};

struct PointKeyIHash {
    size_t operator()(const PointKeyI& p) const {
        return std::hash<int>()(p.x) ^ (std::hash<int>()(p.y) << 1);
    }
};

PointKeyI MakePointKey(const Point& p) {
    PointKeyI k;
    k.x = static_cast<int>(std::round(p.x * 1000.0));
    k.y = static_cast<int>(std::round(p.y * 1000.0));
    return k;
}

class Dsu {
public:
    explicit Dsu(size_t n) : parent_(n), rank_(n, 0) {
        for (size_t i = 0; i < n; ++i) {
            parent_[i] = i;
        }
    }

    size_t Find(size_t x) {
        if (parent_[x] != x) {
            parent_[x] = Find(parent_[x]);
        }
        return parent_[x];
    }

    void Union(size_t a, size_t b) {
        a = Find(a);
        b = Find(b);
        if (a == b) {
            return;
        }
        if (rank_[a] < rank_[b]) {
            parent_[a] = b;
        } else if (rank_[a] > rank_[b]) {
            parent_[b] = a;
        } else {
            parent_[b] = a;
            rank_[a]++;
        }
    }

    size_t Add() {
        const size_t id = parent_.size();
        parent_.push_back(id);
        rank_.push_back(0);
        return id;
    }

private:
    std::vector<size_t> parent_;
    std::vector<size_t> rank_;
};

// Keys on which two bus connections that meet are matched member by member.
//
// A group bus matches on the member's local name, so I2C{SCL, SDA} and
// I2C_54{SCL, SDA} both key "SCL"/"SDA". A vector bus matches on the member's
// position in the range: PC_D[0..7] member 3 meets DQ[0..31] member 3, and the
// four F_IO[0..7] sheet pins of vme-wren's io_driver_32x meet the F_IO[0..7],
// F_IO[8..15], F_IO[16..23] and F_IO[24..31] buses they each sit on. Widths
// need not agree — a narrow bus matches the leading members of a wider one.
std::vector<std::string> BusMemberMatchKeys(const std::string& bus_name,
                                            const std::vector<std::string>& members) {
    std::vector<std::string> keys;
    keys.reserve(members.size());
    std::string prefix;
    if (ParseBusGroup(bus_name, &prefix, nullptr)) {
        while (!prefix.empty() &&
               std::isspace(static_cast<unsigned char>(prefix.back()))) {
            prefix.pop_back();
        }
        for (const auto& m : members) {
            if (!prefix.empty() && m.size() > prefix.size() + 1 &&
                m.compare(0, prefix.size(), prefix) == 0 && m[prefix.size()] == '.') {
                keys.push_back(m.substr(prefix.size() + 1));
            } else {
                keys.push_back(m);
            }
        }
        return keys;
    }
    for (size_t i = 0; i < members.size(); ++i) {
        keys.push_back("#" + std::to_string(i));
    }
    return keys;
}

uint32_t FindNetIdForNode(const NetResolver& resolver, uint32_t node_id) {
    for (const auto& net : resolver.GetNets()) {
        for (uint32_t nid : net.node_ids) {
            if (nid == node_id) {
                return net.id;
            }
        }
    }
    return UINT32_MAX;
}

uint32_t FindNetIdAtPoint(const NetResolver& resolver, const Point& p) {
    const PointKeyI key = MakePointKey(p);
    for (const auto& node : resolver.GetNodes()) {
        if (MakePointKey(node.position) == key) {
            return FindNetIdForNode(resolver, node.id);
        }
    }
    return UINT32_MAX;
}

}  // namespace

ConnectivityGraph BuildGraph(const ProjectInput& input) {
    if (input.entry_file.empty()) {
        throw std::runtime_error("ProjectInput.entry_file is empty");
    }

    StdoutSilence silence;

    const std::vector<LoadedSheet> sheets = LoadProjectSheets(input.entry_file);
    BusAliasMap bus_aliases = LoadBusAliasesFromProject(input.entry_file);
    // A `(bus_alias ...)` declared in any sheet is visible hierarchy-wide, just
    // like one stored in the project file (cm5_minima, jetson, vme-wren keep
    // theirs in the sheets). Project-level definitions win on a name clash.
    for (const auto& sheet : sheets) {
        for (const auto& alias : sheet.schematic.bus_aliases) {
            bus_aliases.emplace(alias.name, alias.members);
        }
    }

    ConnectivityGraph graph;
    graph.schema_version = "1.0";
    graph.source.project_name = StemName(input.entry_file);
    graph.source.entry_file = input.entry_file;
    graph.source.revision = input.revision.empty() ? "working-tree" : input.revision;

    {
        std::vector<std::string> bases;
        bases.reserve(sheets.size());
        for (const auto& s : sheets) {
            bases.push_back(Basename(s.path));
        }
        std::sort(bases.begin(), bases.end());
        bases.erase(std::unique(bases.begin(), bases.end()), bases.end());
        graph.source.sheet_files = std::move(bases);
    }

    std::vector<SheetLocalNet> local_nets;
    std::vector<std::unordered_map<uint32_t, size_t>> sheet_net_index(sheets.size());
    // One entry per sheet pin instance: geometric net on the parent sheet.
    struct SheetPinBinding {
        size_t parent_si = 0;
        std::string child_name;
        std::string pin_name;
        size_t local_net_idx = SIZE_MAX;
        Point location{};
    };
    std::vector<SheetPinBinding> sheet_pin_bindings;
    std::unordered_map<std::string, size_t> path_to_sheet;
    for (size_t i = 0; i < sheets.size(); ++i) {
        path_to_sheet[sheets[i].sheet_path] = i;
    }

    for (size_t si = 0; si < sheets.size(); ++si) {
        Schematic sch = sheets[si].schematic;

        for (auto& comp : sch.components) {
            ApplyInstanceReference(comp, sheets[si].uuid_path);
            ApplyInstanceUnit(comp, sheets[si].uuid_path);
            PinTransform::ComputeWorldPins(comp);
        }

        NetResolver resolver(sch);
        resolver.Resolve();

        PinMapper mapper(sch, resolver);
        mapper.Build();

        // "Exclude from board" symbols are left out of KiCad's netlist export
        // entirely — component and pins alike. They still hold their geometry,
        // so nets routed past them are unaffected. `(dnp yes)` alone stays in.
        std::unordered_set<std::string> excluded_refs;
        for (const auto& src : sch.components) {
            if (!src.on_board && !src.reference.empty()) {
                excluded_refs.insert(src.reference);
            }
        }

        for (const auto& src : sch.components) {
            if (!src.reference.empty() && src.reference[0] == '#') {
                continue;
            }
            if (!src.on_board) {
                continue;
            }

            Component c;
            c.ref = src.reference;
            c.value = src.value;
            c.footprint = src.footprint;
            c.lib_id = src.lib_id;
            c.sheet_path = sheets[si].sheet_path;
            c.x = src.location.x;
            c.y = src.location.y;
            c.rotation = src.rotation;

            for (const auto& pin : src.pins) {
                Pin p;
                p.component_ref = src.reference;
                p.number = pin.number;
                p.name = pin.name;
                p.unit = src.unit;
                c.pins.push_back(std::move(p));
            }
            std::sort(c.pins.begin(), c.pins.end(), [](const Pin& a, const Pin& b) {
                if (a.number != b.number) {
                    return a.number < b.number;
                }
                return a.name < b.name;
            });
            graph.components.push_back(std::move(c));
        }

        std::unordered_map<uint32_t, size_t> net_id_to_local;

        auto EnsureLocalNet = [&](uint32_t net_id, const std::string& net_name) -> size_t {
            auto it = net_id_to_local.find(net_id);
            if (it != net_id_to_local.end()) {
                return it->second;
            }
            SheetLocalNet ln;
            ln.sheet_path = sheets[si].sheet_path;
            ln.name = net_name;
            const size_t idx = local_nets.size();
            local_nets.push_back(std::move(ln));
            net_id_to_local[net_id] = idx;
            sheet_net_index[si][net_id] = idx;
            return idx;
        };

        for (const auto& conn : mapper.GetConnections()) {
            if (conn.net_id == UINT32_MAX) {
                continue;
            }
            if (excluded_refs.count(conn.component_ref) != 0) {
                continue;
            }
            const size_t idx = EnsureLocalNet(conn.net_id, conn.net_name);
            local_nets[idx].pins.push_back(conn.component_ref + "." + conn.pin_number);
            if (!conn.net_name.empty() && local_nets[idx].name.empty()) {
                local_nets[idx].name = conn.net_name;
            }
        }

        // Pins that never snapped to geometry (often no-connect) still appear in
        // KiCad netlists as singleton unconnected-(...) nets.
        // Multi-unit parts: a pin number mapped on ANY unit counts as mapped.
        // Hidden power_in pins are implicit globals (not unconnected, not geometric).
        {
            std::unordered_set<std::string> mapped;
            for (const auto& conn : mapper.GetConnections()) {
                if (conn.net_id == UINT32_MAX) {
                    continue;
                }
                mapped.insert(conn.component_ref + "." + conn.pin_number);
            }
            std::unordered_set<std::string> emitted_unconnected;
            std::unordered_set<std::string> emitted_hidden_power;
            for (const auto& src : sch.components) {
                if (!src.reference.empty() && src.reference[0] == '#') {
                    continue;
                }
                if (!src.on_board) {
                    continue;
                }
                for (const auto& pin : src.pins) {
                    const std::string pid = src.reference + "." + pin.number;
                    // A hidden power_in pin is an implicit global only where it
                    // has no geometry of its own. Parts routinely stack the
                    // extra supply pins on top of a visible one (jetson U21:
                    // eight hidden VDD33 pins sharing pin 26's position), and
                    // those belong to the net that pin is wired to.
                    if (pin.hidden && pin.electrical_type == "power_in" &&
                        !pin.name.empty() && mapped.count(pid) == 0) {
                        if (!emitted_hidden_power.insert(pid).second) {
                            continue;
                        }
                        SheetLocalNet ln;
                        ln.sheet_path = sheets[si].sheet_path;
                        ln.name = UnescapeKicadText(pin.name);
                        ln.is_global = true;
                        ln.global_names.insert(ln.name);
                        ln.pins.push_back(pid);
                        local_nets.push_back(std::move(ln));
                        mapped.insert(pid);
                        continue;
                    }
                    if (mapped.count(pid) != 0) {
                        continue;
                    }
                    if (!emitted_unconnected.insert(pid).second) {
                        continue;
                    }
                    SheetLocalNet ln;
                    ln.sheet_path = sheets[si].sheet_path;
                    ln.name = "unconnected-(" + src.reference + "-Pad" + pin.number + ")";
                    ln.pins.push_back(pid);
                    local_nets.push_back(std::move(ln));
                }
            }
        }

        for (const auto& label : sch.labels) {
            const uint32_t net_id = FindNetIdAtPoint(resolver, label.location);
            if (net_id == UINT32_MAX) {
                continue;
            }
            const size_t idx = EnsureLocalNet(net_id, label.name);
            if (!label.name.empty() && local_nets[idx].name.empty()) {
                local_nets[idx].name = label.name;
            }
            if (label.type == LabelType::Global) {
                local_nets[idx].is_global = true;
                local_nets[idx].global_names.insert(label.name);
            } else if (label.type == LabelType::Hierarchical) {
                local_nets[idx].is_hierarchical = true;
                local_nets[idx].has_hierarchical_label = true;
                local_nets[idx].hierarchical_names.insert(label.name);
                // Bus-form hierarchical labels keep the bus name only.
                // Expanded members are matched at join time — inserting every
                // member here makes one net match all member lookups.
            } else {
                // Local label — same-sheet name merge namespace (not sheet pins).
                local_nets[idx].has_local_label = true;
                if (!label.name.empty()) {
                    local_nets[idx].name = label.name;
                }
            }
        }

        for (const auto& inst : sch.sheets) {
            for (const auto& pin : inst.pins) {
                if (inst.name.empty()) {
                    continue;
                }
                SheetPinBinding b;
                b.parent_si = si;
                b.child_name = inst.name;
                b.pin_name = pin.name;
                b.location = pin.location;
                const uint32_t net_id = FindNetIdAtPoint(resolver, pin.location);
                if (net_id != UINT32_MAX) {
                    const size_t idx = EnsureLocalNet(net_id, pin.name);
                    if (!pin.name.empty() && local_nets[idx].name.empty()) {
                        local_nets[idx].name = pin.name;
                    }
                    local_nets[idx].hierarchical_names.insert(pin.name);
                    local_nets[idx].is_hierarchical = true;
                    // Do NOT expand bus pin members into hierarchical_names —
                    // that would make one net appear to own every member name.
                    b.local_net_idx = idx;
                }
                sheet_pin_bindings.push_back(std::move(b));
            }
        }
    }

    // 02_DATA_MODEL.md §2.2: a multi-unit part is ONE Component whose units
    // contribute distinct pins. KiCad places each unit as its own symbol
    // instance — sometimes on different sheets (vme-wren's IC14 spans nine) —
    // so instances sharing a ComponentId are merged here. Otherwise
    // ComponentId is not the unique identity the diff joins on (03 §3).
    // Ordering by (id, unit, sheet, position) makes the surviving record's
    // sheet_path and cosmetic fields deterministic rather than a by-product of
    // an unstable sort: the part is reported on its first unit's sheet.
    {
        auto min_unit = [](const Component& c) {
            int unit = INT_MAX;
            for (const auto& p : c.pins) {
                unit = std::min(unit, p.unit);
            }
            return unit == INT_MAX ? 1 : unit;
        };
        std::sort(graph.components.begin(), graph.components.end(),
                  [&min_unit](const Component& a, const Component& b) {
                      const std::string ia = MakeComponentId(a);
                      const std::string ib = MakeComponentId(b);
                      if (ia != ib) {
                          return ia < ib;
                      }
                      const int ua = min_unit(a);
                      const int ub = min_unit(b);
                      if (ua != ub) {
                          return ua < ub;
                      }
                      if (a.sheet_path != b.sheet_path) {
                          return a.sheet_path < b.sheet_path;
                      }
                      if (a.x != b.x) {
                          return a.x < b.x;
                      }
                      if (a.y != b.y) {
                          return a.y < b.y;
                      }
                      return a.rotation < b.rotation;
                  });

        std::vector<Component> merged;
        merged.reserve(graph.components.size());
        std::unordered_set<std::string> pin_numbers;
        for (auto& c : graph.components) {
            if (!merged.empty() &&
                MakeComponentId(merged.back()) == MakeComponentId(c)) {
                // PinId is ref.number (02 §2.1), so a number carried by several
                // units — an op-amp's power pins — is one pin, not several.
                for (auto& p : c.pins) {
                    if (pin_numbers.insert(p.number).second) {
                        merged.back().pins.push_back(std::move(p));
                    }
                }
                continue;
            }
            pin_numbers.clear();
            for (const auto& p : c.pins) {
                pin_numbers.insert(p.number);
            }
            merged.push_back(std::move(c));
        }
        for (auto& c : merged) {
            std::sort(c.pins.begin(), c.pins.end(), [](const Pin& a, const Pin& b) {
                if (a.number != b.number) {
                    return a.number < b.number;
                }
                return a.name < b.name;
            });
        }
        graph.components = std::move(merged);
    }

    Dsu dsu(local_nets.size());

    // Multi-unit shared pins (e.g. op-amp power pins on every unit): the same
    // ref.pin may attach via several geometries. If those land on different
    // local nets within one sheet, union them — they are one electrical pin.
    {
        std::unordered_map<std::string, size_t> pin_first;
        for (size_t i = 0; i < local_nets.size(); ++i) {
            for (const auto& p : local_nets[i].pins) {
                const std::string key = local_nets[i].sheet_path + "\n" + p;
                auto it = pin_first.find(key);
                if (it == pin_first.end()) {
                    pin_first[key] = i;
                } else {
                    dsu.Union(it->second, i);
                }
            }
        }
    }

    // Power (#PWR → Global) and global_label: merge by name across all sheets.
    {
        std::unordered_map<std::string, size_t> global_first;
        for (size_t i = 0; i < local_nets.size(); ++i) {
            if (!local_nets[i].is_global) {
                continue;
            }
            for (const auto& gname : local_nets[i].global_names) {
                auto it = global_first.find(gname);
                if (it == global_first.end()) {
                    global_first[gname] = i;
                } else {
                    dsu.Union(it->second, i);
                }
            }
        }
    }

    // Same-sheet name merge — KiCad namespaces:
    //   local_label "X" connects to other local_labels "X" on the sheet
    //   hierarchical_label "X" connects to other hierarchical_labels "X"
    // Sheet pins with the same name do NOT merge with each other (or with
    // local labels) — each pin is a portal onto its own wire geometry
    // (HALPI2: Vin@J1 ≠ Vin@bucks on the Vscap rail).
    {
        std::unordered_map<std::string, size_t> local_first;
        std::unordered_map<std::string, size_t> hier_label_first;
        for (size_t i = 0; i < local_nets.size(); ++i) {
            const auto& ln = local_nets[i];
            if (ln.is_global) {
                continue;
            }
            if (ln.has_local_label && !ln.name.empty() && !IsBusName(ln.name) &&
                LooksNamed(ln.name)) {
                const std::string key = ln.sheet_path + "\nL\n" + ln.name;
                auto it = local_first.find(key);
                if (it == local_first.end()) {
                    local_first[key] = i;
                } else {
                    dsu.Union(it->second, i);
                }
            }
            if (ln.has_hierarchical_label && !ln.name.empty() && !IsBusName(ln.name) &&
                LooksNamed(ln.name)) {
                const std::string key = ln.sheet_path + "\nHL\n" + ln.name;
                auto it = hier_label_first.find(key);
                if (it == hier_label_first.end()) {
                    hier_label_first[key] = i;
                } else {
                    dsu.Union(it->second, i);
                }
            }
        }
    }

    // --- Bus islands: connect differently-prefixed members on one bus ---
    auto OnBusSeg = [](const Point& p, const Point& a, const Point& b,
                       double eps) -> bool {
        const double abx = b.x - a.x;
        const double aby = b.y - a.y;
        const double apx = p.x - a.x;
        const double apy = p.y - a.y;
        const double ab2 = abx * abx + aby * aby;
        if (ab2 < 1e-12) {
            const double dx = p.x - a.x;
            const double dy = p.y - a.y;
            return dx * dx + dy * dy <= eps * eps;
        }
        const double t = (apx * abx + apy * aby) / ab2;
        if (t < -1e-9 || t > 1.0 + 1e-9) {
            return false;
        }
        const double qx = a.x + t * abx;
        const double qy = a.y + t * aby;
        const double dx = p.x - qx;
        const double dy = p.y - qy;
        return dx * dx + dy * dy <= eps * eps;
    };

    struct BusIsland {
        std::vector<std::string> label_bus_names;  // U31{USB3} from local labels
        std::vector<std::string> pin_bus_names;    // USB3{USB3} sheet pins
        std::unordered_map<std::string, size_t> member_nets;  // name → local_net idx
    };

    // sheet_si → islands
    std::vector<std::vector<BusIsland>> sheet_islands(sheets.size());
    // sheet_si → pin_name@x,y → island index (for sheet pins on buses)
    std::vector<std::unordered_map<std::string, size_t>> pin_island(sheets.size());
    // Alternate bus member names from Prefix{Alias} ↔ other Prefix{Alias} zip.
    std::vector<std::unordered_map<std::string, size_t>> bus_renames(sheets.size());

    for (size_t si = 0; si < sheets.size(); ++si) {
        const auto& sch = sheets[si].schematic;
        if (sch.buses.empty()) {
            continue;
        }
        std::unordered_map<PointKeyI, size_t, PointKeyIHash> pt_id;
        std::vector<PointKeyI> pts;
        auto Pid = [&](const Point& p) -> size_t {
            const PointKeyI k = MakePointKey(p);
            auto it = pt_id.find(k);
            if (it != pt_id.end()) {
                return it->second;
            }
            const size_t id = pts.size();
            pts.push_back(k);
            pt_id[k] = id;
            return id;
        };
        for (const auto& seg : sch.buses) {
            Pid(seg.start);
            Pid(seg.end);
        }
        Dsu bus_dsu(pts.size());
        for (const auto& seg : sch.buses) {
            bus_dsu.Union(Pid(seg.start), Pid(seg.end));
        }

        std::unordered_map<size_t, size_t> root_to_island;
        auto IslandOfPoint = [&](const Point& p, double eps) -> size_t {
            for (const auto& seg : sch.buses) {
                if (!OnBusSeg(p, seg.start, seg.end, eps)) {
                    continue;
                }
                const size_t r = bus_dsu.Find(Pid(seg.start));
                auto it = root_to_island.find(r);
                if (it == root_to_island.end()) {
                    const size_t ii = sheet_islands[si].size();
                    sheet_islands[si].emplace_back();
                    root_to_island[r] = ii;
                    return ii;
                }
                return it->second;
            }
            return SIZE_MAX;
        };

        // Bus-form labels on the bus (local or hierarchical). A label's anchor
        // lies exactly on the bus it names, so the tolerance must stay well
        // under the 2.54 mm grid — parallel bus stubs sit one grid step apart
        // and a loose radius attaches a label to its neighbour (video: the
        // TVR[0..7] label landing on the TVG[0..7] stub).
        for (const auto& label : sch.labels) {
            if (!IsBusName(label.name)) {
                continue;
            }
            const size_t ii = IslandOfPoint(label.location, 0.5);
            if (ii == SIZE_MAX) {
                continue;
            }
            sheet_islands[si][ii].label_bus_names.push_back(label.name);
        }

        // Concrete member labels whose geometry touches a bus (via bus_entry).
        for (const auto& label : sch.labels) {
            if (IsBusName(label.name) || label.name.empty()) {
                continue;
            }
            const size_t ii = IslandOfPoint(label.location, 0.5);
            if (ii == SIZE_MAX) {
                continue;
            }
            // Resolve local_net for this label on this sheet.
            for (size_t li = 0; li < local_nets.size(); ++li) {
                if (local_nets[li].sheet_path != sheets[si].sheet_path) {
                    continue;
                }
                if (local_nets[li].has_local_label && local_nets[li].name == label.name) {
                    sheet_islands[si][ii].member_nets[label.name] = li;
                    break;
                }
            }
        }

        // Sheet pins on buses.
        for (const auto& binding : sheet_pin_bindings) {
            if (binding.parent_si != si) {
                continue;
            }
            const size_t ii = IslandOfPoint(binding.location, 0.5);
            if (ii == SIZE_MAX) {
                continue;
            }
            if (IsBusName(binding.pin_name)) {
                sheet_islands[si][ii].pin_bus_names.push_back(binding.pin_name);
            }
            const std::string key = binding.child_name + "\n" + binding.pin_name + "\n" +
                                    std::to_string(MakePointKey(binding.location).x) + "," +
                                    std::to_string(MakePointKey(binding.location).y);
            pin_island[si][key] = ii;
        }

        // Rename zip: Prefix{Alias} label ↔ sheet-pin {Alias} on this island.
        for (auto& island : sheet_islands[si]) {
            for (const auto& lb : island.label_bus_names) {
                const auto left = ExpandBusMembers(lb, bus_aliases);
                if (left.empty()) {
                    continue;
                }
                for (const auto& pb : island.pin_bus_names) {
                    const auto right = ExpandBusMembers(pb, bus_aliases);
                    if (right.size() != left.size()) {
                        continue;
                    }
                    for (size_t c = 0; c < left.size(); ++c) {
                        auto a = island.member_nets.find(left[c]);
                        auto b = island.member_nets.find(right[c]);
                        if (a != island.member_nets.end() && b != island.member_nets.end()) {
                            dsu.Union(a->second, b->second);
                        }
                        if (a != island.member_nets.end()) {
                            bus_renames[si][right[c]] = a->second;
                        }
                        if (b != island.member_nets.end()) {
                            bus_renames[si][left[c]] = b->second;
                        }
                    }
                }
            }
            // Also zip two bus labels on the same island (USB3-1{USB3} hier +
            // USB3{USB3} local, etc.).
            for (size_t i = 0; i < island.label_bus_names.size(); ++i) {
                const auto left = ExpandBusMembers(island.label_bus_names[i], bus_aliases);
                if (left.empty()) {
                    continue;
                }
                for (size_t j = i + 1; j < island.label_bus_names.size(); ++j) {
                    const auto right =
                        ExpandBusMembers(island.label_bus_names[j], bus_aliases);
                    if (right.size() != left.size()) {
                        continue;
                    }
                    for (size_t c = 0; c < left.size(); ++c) {
                        auto a = island.member_nets.find(left[c]);
                        auto b = island.member_nets.find(right[c]);
                        if (a != island.member_nets.end() && b != island.member_nets.end()) {
                            dsu.Union(a->second, b->second);
                        }
                        if (a != island.member_nets.end()) {
                            bus_renames[si][right[c]] = a->second;
                        }
                        if (b != island.member_nets.end()) {
                            bus_renames[si][left[c]] = b->second;
                        }
                    }
                }
            }
        }
    }

    auto FindExactMember = [&](size_t sheet_si, const std::string& n,
                               bool /*prefer_hier*/) -> size_t {
        size_t local_hit = SIZE_MAX;
        size_t hier_hit = SIZE_MAX;
        for (size_t i = 0; i < local_nets.size(); ++i) {
            if (local_nets[i].sheet_path != sheets[sheet_si].sheet_path) {
                continue;
            }
            if (local_nets[i].has_local_label && local_nets[i].name == n) {
                if (local_hit == SIZE_MAX) {
                    local_hit = i;
                }
            }
            if (local_nets[i].hierarchical_names.count(n) != 0) {
                if (hier_hit == SIZE_MAX) {
                    hier_hit = i;
                }
            }
            // Do not match bus-form hierarchical labels via Expand — that
            // aliases every member to one net and shorts the bus.
        }
        // Prefer concrete local member wires over bus-form hier label nets.
        if (local_hit != SIZE_MAX) {
            return local_hit;
        }
        if (hier_hit != SIZE_MAX) {
            return hier_hit;
        }
        auto rit = bus_renames[sheet_si].find(n);
        if (rit != bus_renames[sheet_si].end()) {
            return rit->second;
        }
        // Island-scoped suffix fallback for bus rename (USB3-1.D+ → USB3.D+).
        const auto dot = n.rfind('.');
        if (dot != std::string::npos && dot + 1 < n.size()) {
            const std::string suf = n.substr(dot + 1);
            for (const auto& island : sheet_islands[sheet_si]) {
                bool claims = false;
                for (const auto& bn : island.label_bus_names) {
                    const auto mems = ExpandBusMembers(bn, bus_aliases);
                    if (std::find(mems.begin(), mems.end(), n) != mems.end()) {
                        claims = true;
                        break;
                    }
                }
                for (const auto& bn : island.pin_bus_names) {
                    const auto mems = ExpandBusMembers(bn, bus_aliases);
                    if (std::find(mems.begin(), mems.end(), n) != mems.end()) {
                        claims = true;
                        break;
                    }
                }
                if (!claims) {
                    continue;
                }
                size_t hit = SIZE_MAX;
                int hits = 0;
                for (const auto& kv : island.member_nets) {
                    const auto d2 = kv.first.rfind('.');
                    if (d2 != std::string::npos && kv.first.substr(d2 + 1) == suf) {
                        hit = kv.second;
                        hits++;
                    }
                }
                if (hits == 1) {
                    return hit;
                }
            }
        }
        return SIZE_MAX;
    };

    // Portal key for bus pins that share an island but have no parent member
    // labels (root DIS_USB[0..3] bridging Control MCU ↔ USB3).
    std::unordered_map<std::string, size_t> bus_portals;
    std::unordered_map<std::string, int> bus_pin_fanout;
    for (const auto& binding : sheet_pin_bindings) {
        if (!IsBusName(binding.pin_name)) {
            continue;
        }
        bus_pin_fanout[std::to_string(binding.parent_si) + "\n" + binding.pin_name]++;
    }

    // Sheet pin ↔ child net.
    for (const auto& binding : sheet_pin_bindings) {
        if (binding.child_name.empty()) {
            continue;
        }
        const auto& parent = sheets[binding.parent_si];
        const std::string child_path = parent.sheet_path + binding.child_name + "/";
        auto cit = path_to_sheet.find(child_path);
        if (cit == path_to_sheet.end()) {
            continue;
        }
        const size_t child_si = cit->second;
        const auto pin_members = ExpandBusMembers(binding.pin_name, bus_aliases);
        if (pin_members.empty()) {
            continue;
        }
        const bool is_bus = IsBusName(binding.pin_name) || pin_members.size() > 1;

        if (!is_bus) {
            size_t parent_net = binding.local_net_idx;
            if (parent_net == SIZE_MAX) {
                parent_net = FindExactMember(binding.parent_si, binding.pin_name, false);
            }
            const size_t child_net = FindExactMember(child_si, binding.pin_name, true);
            if (parent_net != SIZE_MAX && child_net != SIZE_MAX) {
                dsu.Union(parent_net, child_net);
            }
            continue;
        }

        const std::string pkey = binding.child_name + "\n" + binding.pin_name + "\n" +
                                 std::to_string(MakePointKey(binding.location).x) + "," +
                                 std::to_string(MakePointKey(binding.location).y);
        size_t ii = SIZE_MAX;
        auto pit = pin_island[binding.parent_si].find(pkey);
        if (pit != pin_island[binding.parent_si].end()) {
            ii = pit->second;
        }

        const std::vector<std::string> member_keys =
            BusMemberMatchKeys(binding.pin_name, pin_members);

        // Bus labels sitting on the same island name this pin's members in the
        // parent sheet's label namespace. Matching is by member key, so a
        // DQ[0..31] label names member 3 of a PC_D[0..7] sheet pin "DQ3" even
        // though the two buses differ in both base name and width.
        std::vector<std::vector<std::string>> island_label_members(pin_members.size());
        std::vector<std::string> parent_member_names = pin_members;
        std::string island_bus_label;
        if (ii != SIZE_MAX) {
            std::vector<std::string> bus_names =
                sheet_islands[binding.parent_si][ii].label_bus_names;
            std::sort(bus_names.begin(), bus_names.end());
            bus_names.erase(std::unique(bus_names.begin(), bus_names.end()),
                            bus_names.end());
            for (const auto& bn : bus_names) {
                const auto mems = ExpandBusMembers(bn, bus_aliases);
                const auto keys = BusMemberMatchKeys(bn, mems);
                for (size_t a = 0; a < mems.size(); ++a) {
                    for (size_t b = 0; b < pin_members.size(); ++b) {
                        if (keys[a] == member_keys[b]) {
                            island_label_members[b].push_back(mems[a]);
                        }
                    }
                }
                if (island_bus_label.empty() && mems.size() == pin_members.size()) {
                    island_bus_label = bn;
                }
            }
            for (size_t b = 0; b < pin_members.size(); ++b) {
                if (!island_label_members[b].empty()) {
                    parent_member_names[b] = island_label_members[b].front();
                }
            }
        }

        auto FindChildBusLabelForMembers = [&](size_t csi,
                                               const std::string& mem) -> std::string {
            for (const auto& lab : sheets[csi].schematic.labels) {
                if (!IsBusName(lab.name)) {
                    continue;
                }
                const auto mems = ExpandBusMembers(lab.name, bus_aliases);
                if (mems.size() == pin_members.size() &&
                    std::find(mems.begin(), mems.end(), mem) != mems.end()) {
                    return lab.name;
                }
            }
            return {};
        };

        auto PortalUnion = [&](const std::string& portal_key, size_t net) {
            auto pit2 = bus_portals.find(portal_key);
            if (pit2 == bus_portals.end()) {
                bus_portals[portal_key] = net;
            } else {
                dsu.Union(pit2->second, net);
            }
        };

        for (size_t mi = 0; mi < pin_members.size(); ++mi) {
            const std::string& child_mem = pin_members[mi];
            const std::string& parent_mem = parent_member_names[mi];
            size_t parent_net = SIZE_MAX;
            if (ii != SIZE_MAX) {
                const auto& island = sheet_islands[binding.parent_si][ii];
                auto it = island.member_nets.find(parent_mem);
                if (it != island.member_nets.end()) {
                    parent_net = it->second;
                } else {
                    it = island.member_nets.find(child_mem);
                    if (it != island.member_nets.end()) {
                        parent_net = it->second;
                    }
                }
            }
            if (parent_net == SIZE_MAX) {
                parent_net = FindExactMember(binding.parent_si, parent_mem, false);
            }
            if (parent_net == SIZE_MAX) {
                parent_net = FindExactMember(binding.parent_si, child_mem, false);
            }
            size_t child_net = FindExactMember(child_si, child_mem, true);
            // Child may use a different prefix on the same alias column
            // (I2C0.SCL vs I2C.SCL). Remap only via bus labels whose expansion
            // matches this pin's member list size and shares the suffix — and
            // prefer labels that share the pin's alias token when present.
            if (child_net == SIZE_MAX) {
                const auto dot = child_mem.rfind('.');
                const std::string suf =
                    (dot == std::string::npos) ? child_mem : child_mem.substr(dot + 1);
                std::string pin_pre;
                std::string pin_alias;
                ParseBusGroup(binding.pin_name, &pin_pre, &pin_alias);
                for (const auto& lab : sheets[child_si].schematic.labels) {
                    if (!IsBusName(lab.name)) {
                        continue;
                    }
                    std::string lab_pre;
                    std::string lab_int;
                    const bool lab_is_group = ParseBusGroup(lab.name, &lab_pre, &lab_int);
                    if (!pin_alias.empty() && (!lab_is_group || lab_int != pin_alias)) {
                        continue;
                    }
                    const auto mems = ExpandBusMembers(lab.name, bus_aliases);
                    if (mems.size() != pin_members.size()) {
                        continue;
                    }
                    const auto d2 = mems[mi].rfind('.');
                    const std::string s2 =
                        (d2 == std::string::npos) ? mems[mi] : mems[mi].substr(d2 + 1);
                    if (s2 != suf) {
                        continue;
                    }
                    // Only accept if this label is the hierarchical counterpart
                    // of *this* pin (same expanded member name as child_mem) —
                    // not a different Prefix{Alias} on the sheet (I2C_DPHY1).
                    if (mems[mi] != child_mem && lab_is_group) {
                        // Different prefix: allow when interiors list the same
                        // members (I2C{SCL, SDA} ↔ I2C_54{SCL, SDA}), or when
                        // the label prefix matches the pin/member prefix
                        // (I2C0{I2C} ↔ I2C{I2C} is NOT auto-allowed — that
                        // needs island context; reject unknown Prefix{Alias}).
                        const auto mem_pre = (dot == std::string::npos)
                                                 ? std::string()
                                                 : child_mem.substr(0, dot);
                        // Only treat as interchangeable prefixes when the braces
                        // hold an explicit member list (commas/spaces), not a
                        // single alias token like {I2C} / {USB3}.
                        const bool inline_list =
                            lab_int.find(',') != std::string::npos ||
                            lab_int.find(' ') != std::string::npos;
                        const bool same_inline_members =
                            inline_list && lab_int == pin_alias;
                        if (lab_pre != mem_pre && lab_pre != pin_pre &&
                            !same_inline_members) {
                            continue;
                        }
                    }
                    child_net = FindExactMember(child_si, mems[mi], true);
                    if (child_net != SIZE_MAX) {
                        break;
                    }
                }
            }
            if (parent_net != SIZE_MAX && child_net != SIZE_MAX) {
                dsu.Union(parent_net, child_net);
                continue;
            }

            // The pin sits on a bus island of the parent sheet, so it shares a
            // portal with every other bus pin on that island, matched member by
            // member whatever the groups are called (UART{TX, RX} ↔
            // UART_TRG{TX, RX}). Additive — the name-keyed portals below still
            // apply — and island scoping keeps buses that share a group name
            // but no wire apart.
            if (child_net != SIZE_MAX && parent_net == SIZE_MAX && ii != SIZE_MAX) {
                PortalUnion(std::to_string(binding.parent_si) + "\nI\n" +
                                std::to_string(ii) + "\n" + member_keys[mi],
                            child_net);
            }

            // Bundle bus with no local member wires on an intermediate sheet:
            // portal through the bus-form label so parent and child sheet pins
            // still meet member-wise (I2C0 on CM4_HS, USB3-0 on USB3, …).
            const std::string child_bus =
                FindChildBusLabelForMembers(child_si, child_mem);
            if (parent_net != SIZE_MAX && child_net == SIZE_MAX) {
                if (!child_bus.empty()) {
                    PortalUnion(std::to_string(child_si) + "\nLM\n" + child_mem,
                                parent_net);
                }
                continue;
            }
            // A bus label on the island puts this member into the parent
            // sheet's label namespace under the label's own member name, so
            // every bus that a label names the same way meets here — including
            // labels of a different width or base name on another island
            // (video: DQ[0..31], DQ[0..15] and DQ[0..7] all naming DQ3).
            if (child_net != SIZE_MAX && parent_net == SIZE_MAX &&
                !island_label_members[mi].empty()) {
                for (const auto& lm : island_label_members[mi]) {
                    PortalUnion(
                        std::to_string(binding.parent_si) + "\nLM\n" + lm, child_net);
                }
                continue;
            }
            // No label names the island (or the pin is on no bus at all): the
            // pin's own bus name supplies the parent-sheet member name. Same
            // namespace as the label case above, so a USB3{USB3} pin under a
            // USB3-0{USB3} label meets a plain USB3-0{USB3} pin elsewhere.
            if (child_net != SIZE_MAX && parent_net == SIZE_MAX &&
                IsBusName(binding.pin_name)) {
                PortalUnion(
                    std::to_string(binding.parent_si) + "\nLM\n" + parent_member_names[mi],
                    child_net);
                continue;
            }
            if (parent_net == SIZE_MAX && child_net == SIZE_MAX) {
                const std::string parent_bus =
                    !island_bus_label.empty()
                        ? island_bus_label
                        : (IsBusName(binding.pin_name) ? binding.pin_name : std::string());
                if (!parent_bus.empty() && !child_bus.empty()) {
                    const std::string key_p = std::to_string(binding.parent_si) +
                                              "\nLM\n" + parent_member_names[mi];
                    const std::string key_c =
                        std::to_string(child_si) + "\nLM\n" + child_mem;
                    auto pit_p = bus_portals.find(key_p);
                    auto pit_c = bus_portals.find(key_c);
                    if (pit_p != bus_portals.end() && pit_c != bus_portals.end()) {
                        dsu.Union(pit_p->second, pit_c->second);
                    } else if (pit_p != bus_portals.end()) {
                        bus_portals[key_c] = pit_p->second;
                    } else if (pit_c != bus_portals.end()) {
                        bus_portals[key_p] = pit_c->second;
                    } else {
                        SheetLocalNet bridge;
                        bridge.sheet_path = sheets[binding.parent_si].sheet_path;
                        bridge.name = parent_bus + "#" + std::to_string(mi);
                        const size_t idx = local_nets.size();
                        local_nets.push_back(std::move(bridge));
                        (void)dsu.Add();
                        bus_portals[key_p] = idx;
                        bus_portals[key_c] = idx;
                    }
                    continue;
                }
            }
            if (child_net == SIZE_MAX) {
                continue;
            }
            const std::string fk =
                std::to_string(binding.parent_si) + "\n" + binding.pin_name;
            const int fanout = bus_pin_fanout[fk];
            std::string portal_key;
            if (ii != SIZE_MAX) {
                portal_key = fk + "\ni" + std::to_string(ii) + "\n" + std::to_string(mi);
            } else if (fanout <= 2) {
                portal_key = fk + "\nx\n" + std::to_string(mi);
            } else {
                continue;
            }
            PortalUnion(portal_key, child_net);
        }
    }

    std::unordered_map<size_t, std::vector<size_t>> groups;
    for (size_t i = 0; i < local_nets.size(); ++i) {
        groups[dsu.Find(i)].push_back(i);
    }

    for (auto& gkv : groups) {
        auto& members = gkv.second;
        std::sort(members.begin(), members.end());

        Net net;
        std::unordered_set<std::string> pin_set;
        bool any_global = false;
        std::string best_name;

        for (size_t mi : members) {
            const auto& ln = local_nets[mi];
            if (ln.is_global) {
                any_global = true;
            }
            if (best_name.empty() && !ln.name.empty()) {
                best_name = ln.name;
            } else if (!ln.name.empty() && ln.is_global) {
                best_name = ln.name;
            } else if (!ln.name.empty() && !LooksNamed(best_name) && LooksNamed(ln.name)) {
                best_name = ln.name;
            }
            for (const auto& p : ln.pins) {
                pin_set.insert(p);
            }
        }

        net.name = best_name;
        net.pins.assign(pin_set.begin(), pin_set.end());
        std::sort(net.pins.begin(), net.pins.end());
        net.is_named = LooksNamed(net.name);
        net.is_power = LooksPower(net.name);
        if (!net.is_power) {
            for (size_t mi : members) {
                for (const auto& g : local_nets[mi].global_names) {
                    if (LooksPower(g)) {
                        net.is_power = true;
                        if (net.name.empty()) {
                            net.name = g;
                        }
                        break;
                    }
                }
                if (net.is_power) {
                    break;
                }
            }
        }
        net.is_named = LooksNamed(net.name);
        net.sheet_scope =
            (any_global || net.is_power) ? "global" : local_nets[members.front()].sheet_path;
        net.net_id = StableHashPinSet(net.pins);

        if (net.pins.empty()) {
            continue;
        }

        // 02_DATA_MODEL.md §2.3: a net with no label still needs a name, and it
        // may be auto-generated so long as `is_named` flags it as such. The
        // label is derived from the lowest PinId, which makes it a function of
        // the pin-set — the net's real identity — so it is deterministic and
        // survives any cosmetic edit. `LooksNamed` already treats the "Net-("
        // prefix as unnamed. This mirrors KiCad's own "Net-(C5-Pad1)" style
        // rather than reproducing its exact pin choice, which is not specified
        // here; net names are never compared against kicad-cli, pin-sets are.
        if (net.name.empty()) {
            const std::string& first = net.pins.front();
            const auto dot = first.rfind('.');
            if (dot == std::string::npos) {
                net.name = "Net-(" + first + ")";
            } else {
                net.name = "Net-(" + first.substr(0, dot) + "-Pad" +
                           first.substr(dot + 1) + ")";
            }
            net.is_named = false;
        }
        if (!net.is_named) {
            graph.stats.unnamed_net_count++;
        }
        graph.nets.push_back(std::move(net));
    }

    std::sort(graph.nets.begin(), graph.nets.end(), [](const Net& a, const Net& b) {
        if (a.net_id != b.net_id) {
            return a.net_id < b.net_id;
        }
        return a.name < b.name;
    });

    graph.stats.component_count = static_cast<int>(graph.components.size());
    graph.stats.net_count = static_cast<int>(graph.nets.size());
    int pin_count = 0;
    for (const auto& c : graph.components) {
        pin_count += static_cast<int>(c.pins.size());
    }
    graph.stats.pin_count = pin_count;

    return graph;
}

}  // namespace netdiff
