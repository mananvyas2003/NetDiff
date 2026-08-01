// Thin CLI over libnetdiff (T0.3). Legacy debug dumps removed from the library path.

#include "netdiff/graph.hpp"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    std::string filepath;
    if (argc > 1) {
        filepath = argv[1];
    } else {
        std::cout << "Usage: netdiff_engine <file.kicad_sch>\n";
        return 2;
    }

    try {
        netdiff::ProjectInput input;
        input.entry_file = filepath;
        const auto graph = netdiff::BuildGraph(input);
        const auto json = netdiff::SerializeGraphJson(graph);
        std::cout << json;
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "ERROR: " << ex.what() << "\n";
        return 3;
    }
}
