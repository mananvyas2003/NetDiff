// T0.4: hierarchical signal spans parent + child sheets (pic_programmer).

#include "netdiff/graph.hpp"

#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: test_cross_sheet_net <pic_programmer.kicad_sch>\n";
        return 2;
    }

    try {
        netdiff::ProjectInput input;
        input.entry_file = argv[1];
        input.revision = "test";
        const netdiff::ConnectivityGraph g = netdiff::BuildGraph(input);

        std::unordered_map<std::string, std::string> ref_to_sheet;
        for (const auto& c : g.components) {
            ref_to_sheet[c.ref] = c.sheet_path;
        }

        bool found_child_sheet = false;
        for (const auto& c : g.components) {
            if (c.sheet_path.find("pic_sockets") != std::string::npos) {
                found_child_sheet = true;
                break;
            }
        }
        if (!found_child_sheet) {
            for (const auto& f : g.source.sheet_files) {
                if (f == "pic_sockets.kicad_sch") {
                    found_child_sheet = true;
                }
            }
        }
        if (!found_child_sheet) {
            std::cerr << "FAIL: pic_sockets sheet not present in graph\n";
            return 1;
        }

        // Look for a net whose pins come from both "/" and a child sheet_path.
        const std::vector<std::string> candidates = {
            "CLOCK-RB6", "DATA-RB7", "VPP-MCLR", "VCC_PIC"};

        bool spanning = false;
        std::string which;
        for (const auto& net : g.nets) {
            bool match_name = false;
            for (const auto& cand : candidates) {
                if (net.name == cand) {
                    match_name = true;
                    break;
                }
            }
            if (!match_name && net.name.find("CLOCK") == std::string::npos &&
                net.name.find("DATA-RB7") == std::string::npos &&
                net.name.find("VPP") == std::string::npos) {
                // Still allow anonymous check via multi sheet_path pins below for named candidates only.
            }
            if (!match_name) {
                continue;
            }

            std::unordered_set<std::string> paths;
            for (const auto& pin_id : net.pins) {
                const auto dot = pin_id.find('.');
                if (dot == std::string::npos) {
                    continue;
                }
                const std::string ref = pin_id.substr(0, dot);
                auto it = ref_to_sheet.find(ref);
                if (it != ref_to_sheet.end()) {
                    paths.insert(it->second);
                }
            }
            if (paths.size() >= 2) {
                spanning = true;
                which = net.name;
                std::cout << "spanning net \"" << net.name << "\" paths:";
                for (const auto& p : paths) {
                    std::cout << " [" << p << "]";
                }
                std::cout << " pins=" << net.pins.size() << "\n";
                break;
            }
        }

        // Fallback: any net with pins from two different sheet_paths
        if (!spanning) {
            for (const auto& net : g.nets) {
                std::unordered_set<std::string> paths;
                for (const auto& pin_id : net.pins) {
                    const auto dot = pin_id.find('.');
                    if (dot == std::string::npos) {
                        continue;
                    }
                    const std::string ref = pin_id.substr(0, dot);
                    auto it = ref_to_sheet.find(ref);
                    if (it != ref_to_sheet.end()) {
                        paths.insert(it->second);
                    }
                }
                if (paths.size() >= 2) {
                    spanning = true;
                    which = net.name.empty() ? net.net_id : net.name;
                    std::cout << "spanning net \"" << which << "\" (fallback) paths=";
                    for (const auto& p : paths) {
                        std::cout << " [" << p << "]";
                    }
                    std::cout << "\n";
                    break;
                }
            }
        }

        if (!spanning) {
            std::cerr << "FAIL: no net spanning multiple sheet_paths "
                         "(expected hierarchical join for CLOCK-RB6 / DATA-RB7 / ...)\n";
            std::cerr << "  components=" << g.stats.component_count
                      << " nets=" << g.stats.net_count << "\n";
            for (const auto& n : g.nets) {
                if (!n.name.empty()) {
                    std::cerr << "  net: " << n.name << " pins=" << n.pins.size() << "\n";
                }
            }
            return 1;
        }

        std::cout << "PASS: cross-sheet net \"" << which << "\"\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "FAIL: " << ex.what() << "\n";
        return 1;
    }
}
