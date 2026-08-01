#include "netdiff/graph.hpp"

#include "interpreter.h"
#include "lexer.h"
#include "net_resolver.h"
#include "parser.h"
#include "pin_mapper.h"
#include "pin_transform.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
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
    // KiCad-style auto names and empty placeholders.
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
    if (!name.empty() && (name[0] == '+' || name[0] == '-')) {
        return true;
    }
    if (name.rfind("VCC", 0) == 0 || name.rfind("VDD", 0) == 0 ||
        name.rfind("VSS", 0) == 0 || name.rfind("VBAT", 0) == 0) {
        return true;
    }
    return false;
}

std::string StableHashPinSet(const std::vector<std::string>& pins_sorted) {
    // FNV-1a 64-bit over pin ids joined by '\n' — deterministic, no hash-map order.
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

std::vector<char> ReadFileBytes(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open schematic: " + path);
    }
    const auto size = static_cast<size_t>(file.tellg());
    file.seekg(0);
    std::vector<char> buffer(size + 1);
    if (size > 0) {
        file.read(buffer.data(), static_cast<std::streamsize>(size));
    }
    buffer[size] = '\0';
    return buffer;
}

}  // namespace

ConnectivityGraph BuildGraph(const ProjectInput& input) {
    if (input.entry_file.empty()) {
        throw std::runtime_error("ProjectInput.entry_file is empty");
    }

    StdoutSilence silence;

    auto buffer = ReadFileBytes(input.entry_file);

    Lexer lexer(buffer.data());
    Parser parser(lexer);
    const uint32_t root = parser.Parse();
    if (root == 0) {
        throw std::runtime_error("Parser returned empty AST for: " + input.entry_file);
    }

    Interpreter interpreter(parser.GetPool());
    Schematic schematic = interpreter.Execute(root);

    for (auto& comp : schematic.components) {
        PinTransform::ComputeWorldPins(comp);
    }

    NetResolver resolver(schematic);
    resolver.Resolve();

    PinMapper mapper(schematic, resolver);
    mapper.Build();

    ConnectivityGraph graph;
    graph.schema_version = "1.0";
    graph.source.project_name = StemName(input.entry_file);
    graph.source.entry_file = input.entry_file;
    graph.source.sheet_files = {Basename(input.entry_file)};
    graph.source.revision = input.revision.empty() ? "working-tree" : input.revision;

    // Components
    graph.components.reserve(schematic.components.size());
    for (const auto& src : schematic.components) {
        // Skip power / annotation symbols from the component inventory.
        if (!src.reference.empty() && src.reference[0] == '#') {
            continue;
        }

        Component c;
        c.ref = src.reference;
        c.value = src.value;
        c.footprint = src.footprint;
        c.lib_id = src.lib_id;
        c.sheet_path = "/";
        c.x = src.location.x;
        c.y = src.location.y;
        c.rotation = src.rotation;

        for (const auto& pin : src.pins) {
            Pin p;
            p.component_ref = src.reference;
            p.number = pin.number;
            p.name = pin.name;
            p.unit = 1;
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

    std::sort(graph.components.begin(), graph.components.end(),
              [](const Component& a, const Component& b) {
                  return MakeComponentId(a) < MakeComponentId(b);
              });

    // Nets: group pin connections by net_id from PinMapper / resolver name.
    std::unordered_map<std::string, std::vector<std::string>> net_to_pins;
    std::unordered_map<std::string, std::string> net_display_name;

    for (const auto& conn : mapper.GetConnections()) {
        if (conn.net_id == UINT32_MAX) {
            continue;
        }
        std::string key = conn.net_name.empty()
                              ? ("__anon_" + std::to_string(conn.net_id))
                              : conn.net_name;
        std::string pin_id = conn.component_ref + "." + conn.pin_number;
        net_to_pins[key].push_back(pin_id);
        net_display_name[key] = conn.net_name.empty() ? key : conn.net_name;
    }

    for (auto& kv : net_to_pins) {
        auto& pins = kv.second;
        std::sort(pins.begin(), pins.end());
        pins.erase(std::unique(pins.begin(), pins.end()), pins.end());

        Net net;
        net.name = net_display_name[kv.first];
        net.pins = pins;
        net.is_named = LooksNamed(net.name);
        net.is_power = LooksPower(net.name);
        net.sheet_scope = (net.is_power || net.is_named) ? "global" : "/";
        net.net_id = StableHashPinSet(net.pins);
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
