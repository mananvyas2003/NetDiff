#include "netdiff/graph.hpp"
#include "netdiff/project_loader.hpp"

#include "interpreter.h"
#include "net_resolver.h"
#include "pin_mapper.h"
#include "pin_transform.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
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

struct SheetLocalNet {
    std::string sheet_path;
    std::string name;
    bool is_global = false;
    bool is_hierarchical = false;
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

private:
    std::vector<size_t> parent_;
    std::vector<size_t> rank_;
};

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
    std::unordered_map<std::string, size_t> path_to_sheet;
    for (size_t i = 0; i < sheets.size(); ++i) {
        path_to_sheet[sheets[i].sheet_path] = i;
    }

    for (size_t si = 0; si < sheets.size(); ++si) {
        Schematic sch = sheets[si].schematic;

        for (auto& comp : sch.components) {
            ApplyInstanceReference(comp, sheets[si].uuid_path);
            PinTransform::ComputeWorldPins(comp);
        }

        NetResolver resolver(sch);
        resolver.Resolve();

        PinMapper mapper(sch, resolver);
        mapper.Build();

        for (const auto& src : sch.components) {
            if (!src.reference.empty() && src.reference[0] == '#') {
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
            const size_t idx = EnsureLocalNet(conn.net_id, conn.net_name);
            local_nets[idx].pins.push_back(conn.component_ref + "." + conn.pin_number);
            if (!conn.net_name.empty() && local_nets[idx].name.empty()) {
                local_nets[idx].name = conn.net_name;
            }
        }

        // Pins that never snapped to geometry (often no-connect) still appear in
        // KiCad netlists as singleton unconnected-(...) nets.
        // Multi-unit parts: a pin number mapped on ANY unit counts as mapped.
        {
            std::unordered_set<std::string> mapped;
            for (const auto& conn : mapper.GetConnections()) {
                if (conn.net_id == UINT32_MAX) {
                    continue;
                }
                mapped.insert(conn.component_ref + "." + conn.pin_number);
            }
            std::unordered_set<std::string> emitted_unconnected;
            for (const auto& src : sch.components) {
                if (!src.reference.empty() && src.reference[0] == '#') {
                    continue;
                }
                for (const auto& pin : src.pins) {
                    const std::string pid = src.reference + "." + pin.number;
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
                local_nets[idx].hierarchical_names.insert(label.name);
            }
        }

        for (const auto& inst : sch.sheets) {
            for (const auto& pin : inst.pins) {
                const uint32_t net_id = FindNetIdAtPoint(resolver, pin.location);
                if (net_id == UINT32_MAX) {
                    continue;
                }
                const size_t idx = EnsureLocalNet(net_id, pin.name);
                if (!pin.name.empty() && local_nets[idx].name.empty()) {
                    local_nets[idx].name = pin.name;
                }
                local_nets[idx].hierarchical_names.insert(pin.name);
                local_nets[idx].is_hierarchical = true;
            }
        }
    }

    std::sort(graph.components.begin(), graph.components.end(),
              [](const Component& a, const Component& b) {
                  return MakeComponentId(a) < MakeComponentId(b);
              });

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

    // Sheet pin ↔ child hierarchical_label
    for (size_t si = 0; si < sheets.size(); ++si) {
        const auto& parent = sheets[si];
        for (const auto& inst : parent.schematic.sheets) {
            if (inst.name.empty()) {
                continue;
            }
            const std::string child_path = parent.sheet_path + inst.name + "/";
            auto cit = path_to_sheet.find(child_path);
            if (cit == path_to_sheet.end()) {
                continue;
            }
            const size_t child_si = cit->second;

            for (const auto& pin : inst.pins) {
                size_t parent_net = SIZE_MAX;
                for (const auto& kv : sheet_net_index[si]) {
                    const size_t idx = kv.second;
                    if (local_nets[idx].hierarchical_names.count(pin.name) != 0 ||
                        local_nets[idx].name == pin.name) {
                        parent_net = idx;
                        break;
                    }
                }

                size_t child_net = SIZE_MAX;
                for (const auto& kv : sheet_net_index[child_si]) {
                    const size_t idx = kv.second;
                    if (local_nets[idx].hierarchical_names.count(pin.name) != 0) {
                        child_net = idx;
                        break;
                    }
                }

                if (parent_net != SIZE_MAX && child_net != SIZE_MAX) {
                    dsu.Union(parent_net, child_net);
                }
            }
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
