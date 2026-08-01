#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace netdiff {

struct ProjectInput {
    std::string entry_file;
    std::string revision;  // optional; empty if unknown
};

struct Pin {
    std::string component_ref;
    std::string number;
    std::string name;
    int unit = 1;
};

inline std::string MakePinId(const Pin& pin) {
    return pin.component_ref + "." + pin.number;
}

struct Component {
    std::string ref;
    std::string value;
    std::string footprint;
    std::string lib_id;
    std::string sheet_path = "/";
    std::vector<Pin> pins;
    // cosmetic (optional):
    double x = 0.0;
    double y = 0.0;
    double rotation = 0.0;
};

inline std::string MakeComponentId(const Component& c) {
    return c.sheet_path + c.ref;
}

struct Net {
    std::string name;
    bool is_named = false;
    bool is_power = false;
    std::vector<std::string> pins;  // PinIds, sorted
    std::string sheet_scope = "global";
    std::string net_id;  // stable hash of sorted pin-set
};

struct GraphSource {
    std::string project_name;
    std::string entry_file;
    std::vector<std::string> sheet_files;
    std::string revision;
};

struct GraphStats {
    int component_count = 0;
    int net_count = 0;
    int pin_count = 0;
    int unnamed_net_count = 0;
};

struct ConnectivityGraph {
    std::string schema_version = "1.0";
    GraphSource source;
    std::vector<Component> components;  // sorted by ComponentId
    std::vector<Net> nets;              // sorted by NetId
    GraphStats stats;
};

// Build a canonical connectivity graph from a KiCad schematic project entry.
ConnectivityGraph BuildGraph(const ProjectInput& input);

// Deterministic JSON: sorted keys/arrays, identical inputs → byte-identical output.
std::string SerializeGraphJson(const ConnectivityGraph& graph);

}  // namespace netdiff
