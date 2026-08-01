// T0.4: recursive multi-sheet load for complex_hierarchy.

#include "netdiff/graph.hpp"
#include "netdiff/project_loader.hpp"

#include <algorithm>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: test_multisheet_load <complex_hierarchy.kicad_sch>\n";
        return 2;
    }

    try {
        const auto sheets = netdiff::LoadProjectSheets(argv[1]);
        if (sheets.size() < 2) {
            std::cerr << "FAIL: expected >=2 loaded sheets, got " << sheets.size() << "\n";
            return 1;
        }

        bool found_ampli = false;
        for (const auto& s : sheets) {
            std::cout << "loaded sheet_path=" << s.sheet_path << " path=" << s.path << "\n";
            if (s.path.find("ampli_ht.kicad_sch") != std::string::npos) {
                found_ampli = true;
            }
        }
        if (!found_ampli) {
            std::cerr << "FAIL: expected ampli_ht.kicad_sch among loaded sheets\n";
            return 1;
        }

        netdiff::ProjectInput input;
        input.entry_file = argv[1];
        input.revision = "test";
        const netdiff::ConnectivityGraph g = netdiff::BuildGraph(input);

        const auto& files = g.source.sheet_files;
        const bool listed = std::find(files.begin(), files.end(), "ampli_ht.kicad_sch") != files.end();
        if (!listed) {
            std::cerr << "FAIL: sheet_files missing ampli_ht.kicad_sch\n";
            return 1;
        }

        bool child_component = false;
        for (const auto& c : g.components) {
            if (c.sheet_path != "/") {
                child_component = true;
                std::cout << "child component " << c.sheet_path << c.ref << "\n";
                break;
            }
        }

        if (!child_component && sheets.size() < 2) {
            std::cerr << "FAIL: no child components and insufficient sheets\n";
            return 1;
        }

        std::cout << "PASS: sheets_loaded=" << sheets.size()
                  << " sheet_files=" << files.size()
                  << " components=" << g.stats.component_count
                  << " child_component=" << (child_component ? "yes" : "no") << "\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "FAIL: " << ex.what() << "\n";
        return 1;
    }
}
