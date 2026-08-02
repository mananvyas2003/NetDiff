#include "netdiff/graph.hpp"

#include <iomanip>
#include <sstream>
#include <string>

#include "json_emit.hpp"

namespace netdiff {

std::string SerializeGraphJson(const ConnectivityGraph& graph) {
    std::ostringstream out;
    out << std::setprecision(17);

    // Keys emitted in lexicographic order at each object level for byte-stability.
    out << "{\n";
    out << "  \"components\": [\n";
    for (size_t i = 0; i < graph.components.size(); ++i) {
        out << "    ";
        json::EmitComponent(out, graph.components[i], "    ");
        if (i + 1 < graph.components.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ],\n";

    out << "  \"nets\": [\n";
    for (size_t i = 0; i < graph.nets.size(); ++i) {
        out << "    ";
        json::EmitNet(out, graph.nets[i], "    ");
        if (i + 1 < graph.nets.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ],\n";

    out << "  \"schema_version\": "; json::EmitString(out, graph.schema_version); out << ",\n";

    out << "  \"source\": {\n";
    out << "    \"entry_file\": "; json::EmitString(out, graph.source.entry_file); out << ",\n";
    out << "    \"project_name\": "; json::EmitString(out, graph.source.project_name); out << ",\n";
    out << "    \"revision\": "; json::EmitString(out, graph.source.revision); out << ",\n";
    out << "    \"sheet_files\": [\n";
    for (size_t i = 0; i < graph.source.sheet_files.size(); ++i) {
        out << "      "; json::EmitString(out, graph.source.sheet_files[i]);
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
