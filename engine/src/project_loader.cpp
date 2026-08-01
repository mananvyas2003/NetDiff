#include "netdiff/project_loader.hpp"

#include "lexer.h"
#include "parser.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace netdiff {
namespace {

namespace fs = std::filesystem;

std::vector<char> ReadFileBytes(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open schematic: " + path);
    }
    const auto size = static_cast<size_t>(file.tellg());
    file.seekg(0);
    std::vector<char> buffer(size + 1);
    if (size > 0) {
        file.read(buffer.data(), static_cast<std::streamsize>(size));
    }
    buffer[size] = '\0';
    return buffer;
}

Schematic ParseSchematicFile(const std::string& path) {
    auto buffer = ReadFileBytes(path);
    Lexer lexer(buffer.data());
    Parser parser(lexer);
    const uint32_t root = parser.Parse();
    if (root == 0) {
        throw std::runtime_error("Parser returned empty AST for: " + path);
    }
    Interpreter interpreter(parser.GetPool());
    return interpreter.Execute(root);
}

std::string JoinSheetPath(const std::string& parent_path, const std::string& sheet_name) {
    return parent_path + sheet_name + "/";
}

std::string JoinUuidPath(const std::string& parent_uuid_path, const std::string& sheet_uuid) {
    if (sheet_uuid.empty()) {
        return parent_uuid_path;
    }
    if (parent_uuid_path.empty() || parent_uuid_path == "/") {
        return "/" + sheet_uuid;
    }
    if (parent_uuid_path.back() == '/') {
        return parent_uuid_path + sheet_uuid;
    }
    return parent_uuid_path + "/" + sheet_uuid;
}

void LoadRecursive(
    const fs::path& file_path,
    const std::string& sheet_path,
    const std::string& uuid_path_in,
    std::unordered_set<std::string>& recursion_stack,
    std::vector<LoadedSheet>& out) {
    std::error_code ec;
    fs::path canonical = fs::weakly_canonical(file_path, ec);
    if (ec) {
        canonical = fs::absolute(file_path, ec);
    }
    if (ec || !fs::exists(canonical)) {
        throw std::runtime_error("Missing hierarchical sheet file: " + file_path.string());
    }

    const std::string key = canonical.string();
    if (recursion_stack.count(key) != 0) {
        throw std::runtime_error("Hierarchical sheet cycle detected at: " + key);
    }

    recursion_stack.insert(key);

    LoadedSheet loaded;
    loaded.path = canonical.string();
    loaded.sheet_path = sheet_path;
    loaded.schematic = ParseSchematicFile(loaded.path);

    std::string uuid_path = uuid_path_in;
    if (uuid_path.empty() || uuid_path == "/") {
        if (!loaded.schematic.uuid.empty()) {
            uuid_path = "/" + loaded.schematic.uuid;
        }
    }
    loaded.uuid_path = uuid_path;

    // Copy fields needed for recursion before moving `loaded` into `out`
    // (push_back may reallocate and invalidate references).
    const std::string self_sheet_path = loaded.sheet_path;
    const std::string self_uuid_path = loaded.uuid_path;
    const fs::path parent_dir = canonical.parent_path();
    const std::vector<SheetInstance> children = loaded.schematic.sheets;

    out.push_back(std::move(loaded));

    for (const auto& inst : children) {
        if (inst.file.empty()) {
            continue;
        }
        const fs::path child_file = parent_dir / inst.file;
        const std::string child_sheet_path = JoinSheetPath(self_sheet_path, inst.name);
        const std::string child_uuid_path = JoinUuidPath(self_uuid_path, inst.uuid);
        LoadRecursive(child_file, child_sheet_path, child_uuid_path, recursion_stack, out);
    }

    recursion_stack.erase(key);
}

}  // namespace

std::vector<LoadedSheet> LoadProjectSheets(const std::string& entry_file) {
    if (entry_file.empty()) {
        throw std::runtime_error("LoadProjectSheets: entry_file is empty");
    }

    std::vector<LoadedSheet> sheets;
    std::unordered_set<std::string> recursion_stack;
    LoadRecursive(fs::path(entry_file), "/", "/", recursion_stack, sheets);
    return sheets;
}

}  // namespace netdiff
