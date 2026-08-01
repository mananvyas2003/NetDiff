// T0.4: parse sheet / Sheetfile / sheet pins from HALPI2 root schematic.

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

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: test_sheet_parse <HALPI2.kicad_sch>\n";
        return 2;
    }

    try {
        auto buf = ReadFile(argv[1]);
        Lexer lexer(buf.data());
        Parser parser(lexer);
        const uint32_t root = parser.Parse();
        Interpreter interp(parser.GetPool());
        Schematic sch = interp.Execute(root);

        if (sch.sheets.empty()) {
            std::cerr << "FAIL: expected >=1 sheet instance\n";
            return 1;
        }

        size_t with_file = 0;
        size_t pin_count = 0;
        for (const auto& s : sch.sheets) {
            if (!s.file.empty()) {
                ++with_file;
            }
            pin_count += s.pins.size();
            std::cout << "sheet name=\"" << s.name << "\" file=\"" << s.file
                      << "\" pins=" << s.pins.size() << "\n";
        }

        if (with_file < 1) {
            std::cerr << "FAIL: expected >=1 sheet with Sheetfile\n";
            return 1;
        }
        if (pin_count < 1) {
            std::cerr << "FAIL: expected >=1 sheet pin name\n";
            return 1;
        }

        std::cout << "PASS: sheets=" << sch.sheets.size()
                  << " with_Sheetfile=" << with_file
                  << " sheet_pins=" << pin_count << "\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "FAIL: " << ex.what() << "\n";
        return 1;
    }
}
