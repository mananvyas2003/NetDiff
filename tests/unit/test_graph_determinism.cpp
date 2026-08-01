// T0.3 AC: two BuildGraph+Serialize runs on the ESP32 sample → byte-identical JSON.

#include "netdiff/graph.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: test_graph_determinism <schematic.kicad_sch>\n";
        return 2;
    }

    netdiff::ProjectInput input;
    input.entry_file = argv[1];
    input.revision = "test";

    const netdiff::ConnectivityGraph g1 = netdiff::BuildGraph(input);
    const netdiff::ConnectivityGraph g2 = netdiff::BuildGraph(input);

    const std::string json1 = netdiff::SerializeGraphJson(g1);
    const std::string json2 = netdiff::SerializeGraphJson(g2);

    if (json1 != json2) {
        std::cerr << "FAIL: SerializeGraphJson outputs differ between two runs\n";
        std::cerr << "  run1 bytes=" << json1.size() << " run2 bytes=" << json2.size() << "\n";
        return 1;
    }

    if (json1.find("\"schema_version\": \"1.0\"") == std::string::npos) {
        std::cerr << "FAIL: missing schema_version 1.0\n";
        return 1;
    }

    if (g1.stats.component_count <= 0) {
        std::cerr << "FAIL: expected components in ESP32 sample, got "
                  << g1.stats.component_count << "\n";
        return 1;
    }

    // Also write once for inspection.
    {
        std::ofstream out("graph_esp32.json", std::ios::binary);
        out << json1;
    }

    std::cout << "PASS: byte-identical JSON (" << json1.size() << " bytes), "
              << "components=" << g1.stats.component_count
              << " nets=" << g1.stats.net_count
              << " pins=" << g1.stats.pin_count << "\n";
    return 0;
}
