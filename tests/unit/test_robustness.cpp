// T0.6 — malformed inputs must not crash; BuildGraph throws / CLI exits 3.

#include "netdiff/graph.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool expect_throw(const std::string& path, const char* label) {
    netdiff::ProjectInput in;
    in.entry_file = path;
    try {
        (void)netdiff::BuildGraph(in);
        std::cerr << "FAIL: " << label << " did not throw\n";
        return false;
    } catch (const std::exception& ex) {
        std::cout << "OK throw (" << label << "): " << ex.what() << "\n";
        return true;
    } catch (...) {
        std::cout << "OK throw (" << label << "): unknown exception\n";
        return true;
    }
}

std::string write_temp(const std::string& name, const std::string& contents) {
    const std::string path = name;
    std::ofstream out(path, std::ios::binary);
    out << contents;
    return path;
}

}  // namespace

int main() {
    int fails = 0;

    if (!expect_throw("definitely_missing_file_netdiff_t06.kicad_sch", "missing file")) {
        ++fails;
    }

    const auto empty = write_temp("t06_empty.kicad_sch", "");
    if (!expect_throw(empty, "empty file")) {
        ++fails;
    }

    const auto garbage = write_temp("t06_garbage.kicad_sch", "\x00\x01\x02 NOT SEXP");
    if (!expect_throw(garbage, "garbage")) {
        ++fails;
    }

    const auto unbalanced = write_temp("t06_unbalanced.kicad_sch", "(kicad_sch (version 20230121");
    if (!expect_throw(unbalanced, "unbalanced parens")) {
        ++fails;
    }

    // Deep nesting — should not stack-overflow / segfault.
    std::string deep = "(kicad_sch";
    for (int i = 0; i < 5000; ++i) {
        deep += " (x";
    }
    for (int i = 0; i < 5000; ++i) {
        deep += ")";
    }
    deep += ")";
    const auto deep_path = write_temp("t06_deep.kicad_sch", deep);
    try {
        netdiff::ProjectInput in;
        in.entry_file = deep_path;
        (void)netdiff::BuildGraph(in);
        // May succeed as empty-ish schematic or throw — either is fine if no crash.
        std::cout << "OK deep nesting completed without crash\n";
    } catch (const std::exception& ex) {
        std::cout << "OK deep nesting threw: " << ex.what() << "\n";
    }

    if (fails) {
        std::cerr << "FAILED " << fails << " robustness checks\n";
        return 1;
    }
    std::cout << "PASS: robustness suite\n";
    return 0;
}
