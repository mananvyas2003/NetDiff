#include "netdiff/graph.hpp"

#include <iomanip>
#include <sstream>
#include <string>

namespace netdiff {
namespace {

std::string EscapeJson(const std::string& input) {
    std::ostringstream ss;
    for (unsigned char c : input) {
        switch (c) {
        case '\\': ss << "\\\\"; break;
        case '"':  ss << "\\\""; break;
        case '\b': ss << "\\b"; break;
        case '\f': ss << "\\f"; break;
        case '\n': ss << "\\n"; break;
        case '\r': ss << "\\r"; break;
        case '\t': ss << "\\t"; break;
        default:
            if (c < 0x20) {
                ss << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                   << static_cast<int>(c) << std::dec;
            } else {
                ss << static_cast<char>(c);
            }
            break;
        }
    }
    return ss.str();
}

void EmitString(std::ostringstream& out, const std::string& value) {
    out << '"' << EscapeJson(value) << '"';
}

void EmitBool(std::ostringstream& out, bool value) {
    out << (value ? "true" : "false");
}

}  // namespace

std::string SerializeGraphJson(const ConnectivityGraph& graph) {
    std::ostringstream out;
    out << std::setprecision(17);

    // Keys emitted in lexicographic order at each object level for byte-stability.
    out << "{\n";
    out << "  \"components\": [\n";
    for (size_t i = 0; i < graph.components.size(); ++i) {
        const auto& c = graph.components[i];
        out << "    {\n";
        out << "      \"footprint\": "; EmitString(out, c.footprint); out << ",\n";
        out << "      \"lib_id\": "; EmitString(out, c.lib_id); out << ",\n";
        out << "      \"pins\": [\n";
        for (size_t p = 0; p < c.pins.size(); ++p) {
            const auto& pin = c.pins[p];
            out << "        {\n";
            out << "          \"component_ref\": "; EmitString(out, pin.component_ref); out << ",\n";
            out << "          \"name\": "; EmitString(out, pin.name); out << ",\n";
            out << "          \"number\": "; EmitString(out, pin.number); out << ",\n";
            out << "          \"unit\": " << pin.unit << "\n";
            out << "        }";
            if (p + 1 < c.pins.size()) {
                out << ",";
            }
            out << "\n";
        }
        out << "      ],\n";
        out << "      \"position\": {\n";
        out << "        \"rotation\": " << c.rotation << ",\n";
        out << "        \"x\": " << c.x << ",\n";
        out << "        \"y\": " << c.y << "\n";
        out << "      },\n";
        out << "      \"ref\": "; EmitString(out, c.ref); out << ",\n";
        out << "      \"sheet_path\": "; EmitString(out, c.sheet_path); out << ",\n";
        out << "      \"value\": "; EmitString(out, c.value); out << "\n";
        out << "    }";
        if (i + 1 < graph.components.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ],\n";

    out << "  \"nets\": [\n";
    for (size_t i = 0; i < graph.nets.size(); ++i) {
        const auto& n = graph.nets[i];
        out << "    {\n";
        out << "      \"is_named\": "; EmitBool(out, n.is_named); out << ",\n";
        out << "      \"is_power\": "; EmitBool(out, n.is_power); out << ",\n";
        out << "      \"name\": "; EmitString(out, n.name); out << ",\n";
        out << "      \"net_id\": "; EmitString(out, n.net_id); out << ",\n";
        out << "      \"pins\": [\n";
        for (size_t p = 0; p < n.pins.size(); ++p) {
            out << "        "; EmitString(out, n.pins[p]);
            if (p + 1 < n.pins.size()) {
                out << ",";
            }
            out << "\n";
        }
        out << "      ],\n";
        out << "      \"sheet_scope\": "; EmitString(out, n.sheet_scope); out << "\n";
        out << "    }";
        if (i + 1 < graph.nets.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ],\n";

    out << "  \"schema_version\": "; EmitString(out, graph.schema_version); out << ",\n";

    out << "  \"source\": {\n";
    out << "    \"entry_file\": "; EmitString(out, graph.source.entry_file); out << ",\n";
    out << "    \"project_name\": "; EmitString(out, graph.source.project_name); out << ",\n";
    out << "    \"revision\": "; EmitString(out, graph.source.revision); out << ",\n";
    out << "    \"sheet_files\": [\n";
    for (size_t i = 0; i < graph.source.sheet_files.size(); ++i) {
        out << "      "; EmitString(out, graph.source.sheet_files[i]);
        if (i + 1 < graph.source.sheet_files.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "    ]\n";
    out << "  },\n";

    out << "  \"stats\": {\n";
    out << "    \"component_count\": " << graph.stats.component_count << ",\n";
    out << "    \"net_count\": " << graph.stats.net_count << ",\n";
    out << "    \"pin_count\": " << graph.stats.pin_count << ",\n";
    out << "    \"unnamed_net_count\": " << graph.stats.unnamed_net_count << "\n";
    out << "  }\n";
    out << "}\n";

    return out.str();
}

}  // namespace netdiff
