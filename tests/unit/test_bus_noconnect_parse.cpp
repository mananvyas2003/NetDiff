// T0.4: parse no_connect points and bus(+bus_entry) segments.

#include "interpreter.h"
#include "lexer.h"
#include "parser.h"

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::vector<char> ReadFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        throw std::runtime_error("cannot open " + path);
    }
    const auto size = static_cast<size_t>(file.tellg());
    file.seekg(0);
    std::vector<char> buf(size + 1);
    if (size > 0) {
        file.read(buf.data(), static_cast<std::streamsize>(size));
    }
    buf[size] = '\0';
    return buf;
}

Schematic ParseFile(const std::string& path) {
    auto buf = ReadFile(path);
    Lexer lexer(buf.data());
    Parser parser(lexer);
    const uint32_t root = parser.Parse();
    Interpreter interp(parser.GetPool());
    return interp.Execute(root);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: test_bus_noconnect_parse <complex_hierarchy.kicad_sch> <interf_u.kicad_sch>\n";
        return 2;
    }

    try {
        const Schematic hierarchy = ParseFile(argv[1]);
        if (hierarchy.no_connects.empty()) {
            std::cerr << "FAIL: expected >=1 no_connect in complex_hierarchy\n";
            return 1;
        }
        std::cout << "no_connect count=" << hierarchy.no_connects.size() << "\n";

        const Schematic interf = ParseFile(argv[2]);
        if (interf.buses.empty()) {
            std::cerr << "FAIL: expected >=1 bus/bus_entry segment in interf_u\n";
            return 1;
        }
        std::cout << "bus segments=" << interf.buses.size() << "\n";

        std::cout << "PASS: no_connect + bus geometry parse\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "FAIL: " << ex.what() << "\n";
        return 1;
    }
}
