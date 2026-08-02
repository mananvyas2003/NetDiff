// T1.5 flagship invariant (03 §7, 06 §3.1): a cosmetically perturbed schematic
// is electrically identical, so the diff must report significant_count == 0.
//
// The perturbations are applied to a throwaway copy of the real project:
//   1. translate every coordinate by a constant — moves every symbol, wire,
//      junction and label without altering what touches what;
//   2. reverse the order of the top-level `wire` and `symbol` blocks — same
//      drawing, different file order, so nothing may depend on parse order.
// The test asserts the copy really did change; a no-op perturbation would make
// the whole check vacuous.
//
// argv[1..] = corpus entry schematics.

#include "netdiff/diff.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using netdiff::ConnectivityGraph;
using netdiff::Diff;
using netdiff::DiffConfig;
using netdiff::DiffResult;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << "\n";
        ++g_failures;
    }
}

std::string ReadFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void WriteFile(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
}

std::string FormatCoordinate(double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.6f", value);
    std::string text(buffer);
    const auto last = text.find_last_not_of('0');
    if (last != std::string::npos) {
        text.erase(text[last] == '.' ? last : last + 1);
    }
    return text;
}

bool IsNumberStart(char c) {
    return (c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.';
}

// Reads one numeric token at `pos`, returns false if there isn't one.
bool ReadNumber(const std::string& text, size_t pos, size_t& end, double& value) {
    if (pos >= text.size() || !IsNumberStart(text[pos])) {
        return false;
    }
    size_t i = pos;
    while (i < text.size() && (IsNumberStart(text[i]) || text[i] == 'e' || text[i] == 'E')) {
        ++i;
    }
    try {
        size_t consumed = 0;
        value = std::stod(text.substr(pos, i - pos), &consumed);
        if (consumed == 0) {
            return false;
        }
        end = pos + consumed;
        return true;
    } catch (...) {
        return false;
    }
}

// Shift the x/y of every `(at x y ...)` and `(xy x y)` by a constant. A uniform
// translation cannot change which pins touch which wires.
std::string TranslateCoordinates(const std::string& text, double dx, double dy) {
    std::string out;
    out.reserve(text.size() + text.size() / 8);
    size_t i = 0;
    while (i < text.size()) {
        bool matched_at = text.compare(i, 4, "(at ") == 0;
        bool matched_xy = text.compare(i, 4, "(xy ") == 0;
        if (!matched_at && !matched_xy) {
            out.push_back(text[i++]);
            continue;
        }
        out.append(text, i, 4);
        size_t pos = i + 4;
        while (pos < text.size() && text[pos] == ' ') {
            out.push_back(text[pos++]);
        }
        double x = 0.0;
        double y = 0.0;
        size_t after_x = 0;
        if (!ReadNumber(text, pos, after_x, x)) {
            i = pos;
            continue;
        }
        size_t gap = after_x;
        while (gap < text.size() && text[gap] == ' ') {
            ++gap;
        }
        size_t after_y = 0;
        if (!ReadNumber(text, gap, after_y, y)) {
            i = pos;
            continue;
        }
        out += FormatCoordinate(x + dx);
        out.append(text, after_x, gap - after_x);
        out += FormatCoordinate(y + dy);
        i = after_y;
    }
    return out;
}

std::vector<std::pair<size_t, size_t>> TopLevelBlocks(const std::string& text);

// What may follow a list head. The corpus is CRLF, so '\r' belongs here —
// leaving it out silently turns every head match below into a miss.
bool IsHeadDelimiter(char c) {
    return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '(' || c == ')';
}

// True if `text` has the list `(head ...)` starting at `pos`.
bool HeadIs(const std::string& text, size_t pos, const std::string& head) {
    const std::string opener = "(" + head;
    if (text.compare(pos, opener.size(), opener) != 0) {
        return false;
    }
    return pos + opener.size() < text.size() &&
           IsHeadDelimiter(text[pos + opener.size()]);
}

// Coordinates inside `(lib_symbols ...)` are relative to the symbol's own
// origin — its pins sit at things like (at 0.635 -2.54). Translating those
// would move pins away from the symbol they belong to and genuinely break
// connectivity, so the block is copied through untouched.
std::string TranslatePlacedCoordinates(const std::string& text, double dx, double dy) {
    const auto blocks = TopLevelBlocks(text);
    for (const auto& block : blocks) {
        if (!HeadIs(text, block.first, "lib_symbols")) {
            continue;
        }
        return TranslateCoordinates(text.substr(0, block.first), dx, dy) +
               text.substr(block.first, block.second - block.first) +
               TranslateCoordinates(text.substr(block.second), dx, dy);
    }
    // No cached symbol definitions to protect.
    return TranslateCoordinates(text, dx, dy);
}

// Split the top-level list into its direct children so whole blocks can move.
std::vector<std::pair<size_t, size_t>> TopLevelBlocks(const std::string& text) {
    std::vector<std::pair<size_t, size_t>> blocks;
    const size_t root = text.find('(');
    if (root == std::string::npos) {
        return blocks;
    }
    int depth = 0;
    size_t start = std::string::npos;
    bool in_string = false;
    for (size_t i = root; i < text.size(); ++i) {
        const char c = text[i];
        if (in_string) {
            if (c == '\\') {
                ++i;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
        } else if (c == '(') {
            ++depth;
            if (depth == 2) {
                start = i;
            }
        } else if (c == ')') {
            if (depth == 2 && start != std::string::npos) {
                blocks.emplace_back(start, i + 1);
                start = std::string::npos;
            }
            --depth;
            if (depth == 0) {
                break;
            }
        }
    }
    return blocks;
}

// Reverse the order of same-kind blocks while leaving every other block where
// it is: the file lists the same elements, in a different sequence.
std::string ReverseBlocksNamed(const std::string& text, const std::string& head) {
    const auto blocks = TopLevelBlocks(text);
    std::vector<std::pair<size_t, size_t>> slots;
    for (const auto& block : blocks) {
        if (HeadIs(text, block.first, head)) {
            slots.push_back(block);
        }
    }
    if (slots.size() < 2) {
        return text;
    }
    std::vector<std::string> bodies;
    for (const auto& slot : slots) {
        bodies.push_back(text.substr(slot.first, slot.second - slot.first));
    }
    std::reverse(bodies.begin(), bodies.end());

    std::string out;
    size_t cursor = 0;
    for (size_t i = 0; i < slots.size(); ++i) {
        out.append(text, cursor, slots[i].first - cursor);
        out += bodies[i];
        cursor = slots[i].second;
    }
    out.append(text, cursor, text.size() - cursor);
    return out;
}

// Copy the project, perturb every sheet in the copy, return the copied entry.
fs::path MakePerturbedCopy(const fs::path& entry, const fs::path& work, bool& changed) {
    const fs::path source_dir = entry.parent_path();
    fs::copy(source_dir, work,
             fs::copy_options::recursive | fs::copy_options::overwrite_existing);

    changed = false;
    for (const auto& file : fs::recursive_directory_iterator(work)) {
        if (!file.is_regular_file() || file.path().extension() != ".kicad_sch") {
            continue;
        }
        const std::string original = ReadFile(file.path());
        std::string perturbed = TranslatePlacedCoordinates(original, 100.0, 50.0);
        perturbed = ReverseBlocksNamed(perturbed, "wire");
        perturbed = ReverseBlocksNamed(perturbed, "symbol");
        // Both perturbations must bite, or the invariant proves nothing.
        if (perturbed != original) {
            changed = true;
        }
        WriteFile(file.path(), perturbed);
    }
    return work / entry.filename();
}

std::vector<std::vector<std::string>> PinSets(const ConnectivityGraph& graph) {
    std::vector<std::vector<std::string>> sets;
    for (const auto& net : graph.nets) {
        sets.push_back(net.pins);
    }
    std::sort(sets.begin(), sets.end());
    return sets;
}

void RunInvariant(const fs::path& entry, const fs::path& work_root) {
    const std::string label = entry.filename().string();
    const fs::path work = work_root / entry.parent_path().filename();
    fs::remove_all(work);

    bool changed = false;
    fs::path perturbed_entry;
    try {
        perturbed_entry = MakePerturbedCopy(entry, work, changed);
    } catch (const std::exception& ex) {
        Check(false, label + ": could not build perturbed copy: " + ex.what());
        return;
    }
    // Without this the invariant would pass trivially on an unmodified file.
    Check(changed, label + ": perturbation actually modified the schematic");

    netdiff::ProjectInput before_input;
    before_input.entry_file = entry.string();
    netdiff::ProjectInput after_input;
    after_input.entry_file = perturbed_entry.string();

    const ConnectivityGraph before = netdiff::BuildGraph(before_input);
    const ConnectivityGraph after = netdiff::BuildGraph(after_input);

    // The perturbation must not have quietly broken the drawing.
    Check(before.components.size() == after.components.size(),
          label + ": perturbed copy keeps the component count (" +
              std::to_string(before.components.size()) + " vs " +
              std::to_string(after.components.size()) + ")");
    Check(PinSets(before) == PinSets(after),
          label + ": perturbed copy keeps every net pin-set");

    const DiffConfig config;
    const DiffResult result = Diff(before, after, config);

    if (result.summary.significant_count != 0) {
        int shown = 0;
        for (const auto& change : result.changes) {
            if (change.significance != netdiff::Significance::kSignificant) {
                continue;
            }
            std::cerr << "    " << netdiff::ToString(change.type()) << ": " << change.message
                      << "\n";
            if (++shown == 5) {
                break;
            }
        }
    }
    Check(result.summary.significant_count == 0,
          label + ": significant_count == 0 after cosmetic perturbation (got " +
              std::to_string(result.summary.significant_count) + ")");
    Check(result.summary.gate == netdiff::GateResult::kPass,
          label + ": gate PASSes on a cosmetic-only change");

    std::cout << "  " << label << ": significant=" << result.summary.significant_count
              << " cosmetic=" << result.summary.cosmetic_count
              << " components=" << before.components.size()
              << " nets=" << before.nets.size() << "\n";

    fs::remove_all(work);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: test_diff_invariant <entry.kicad_sch>...\n";
        return 2;
    }
    const fs::path work_root =
        fs::temp_directory_path() / "netdiff-invariant";
    fs::remove_all(work_root);
    fs::create_directories(work_root);

    for (int i = 1; i < argc; ++i) {
        RunInvariant(fs::path(argv[i]), work_root);
    }

    fs::remove_all(work_root);

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "cosmetic-change invariant: ok\n";
    return 0;
}
