#include "netdiff/diff.hpp"
#include "netdiff/graph.hpp"

#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>

namespace {

char* DupCString(const std::string& text) {
    char* out = static_cast<char*>(std::malloc(text.size() + 1));
    if (out == nullptr) {
        return nullptr;
    }
    std::memcpy(out, text.data(), text.size());
    out[text.size()] = '\0';
    return out;
}

std::string ErrorJson(const std::string& message) {
    std::string escaped;
    escaped.reserve(message.size());
    for (char c : message) {
        if (c == '\\' || c == '"') {
            escaped.push_back('\\');
        }
        if (c == '\n') {
            escaped += "\\n";
        } else {
            escaped.push_back(c);
        }
    }
    return std::string("{\"error\": \"") + escaped + "\"}";
}

}  // namespace

extern "C" {

const char* netdiff_version(void) {
    return "0.1.0";
}

void netdiff_free(char* pointer) {
    std::free(pointer);
}

// Diff two schematic entry paths already present in the Emscripten filesystem.
// Returns a heap JSON string (DiffResult or {"error":...}); caller must netdiff_free().
char* netdiff_diff_paths(const char* before_entry, const char* after_entry) {
    if (before_entry == nullptr || after_entry == nullptr || before_entry[0] == '\0' ||
        after_entry[0] == '\0') {
        return DupCString(ErrorJson("before_entry and after_entry are required"));
    }
    try {
        netdiff::ProjectInput before_in;
        before_in.entry_file = before_entry;
        before_in.revision = "before";
        netdiff::ProjectInput after_in;
        after_in.entry_file = after_entry;
        after_in.revision = "after";

        const netdiff::ConnectivityGraph before = netdiff::BuildGraph(before_in);
        const netdiff::ConnectivityGraph after = netdiff::BuildGraph(after_in);
        const netdiff::DiffResult result = netdiff::Diff(before, after, netdiff::DiffConfig{});
        return DupCString(netdiff::SerializeDiffJson(result));
    } catch (const std::exception& ex) {
        return DupCString(ErrorJson(ex.what()));
    } catch (...) {
        return DupCString(ErrorJson("internal error"));
    }
}

}  // extern "C"
