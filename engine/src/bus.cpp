#include "netdiff/bus.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace netdiff {
namespace {

namespace fs = std::filesystem;

bool ParseIntToken(const std::string& s, int& out) {
    if (s.empty()) {
        return false;
    }
    size_t i = 0;
    if (s[0] == '+' || s[0] == '-') {
        i = 1;
    }
    if (i >= s.size()) {
        return false;
    }
    for (; i < s.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) {
            return false;
        }
    }
    out = std::atoi(s.c_str());
    return out >= 0;
}

std::vector<std::string> ExpandVector(const std::string& name) {
    const auto lb = name.rfind('[');
    const auto rb = name.rfind(']');
    if (lb == std::string::npos || rb == std::string::npos || rb <= lb + 1) {
        return {};
    }
    if (rb != name.size() - 1) {
        return {};
    }
    const std::string prefix = name.substr(0, lb);
    const std::string inside = name.substr(lb + 1, rb - lb - 1);
    const auto dots = inside.find("..");
    if (dots == std::string::npos) {
        return {};
    }
    const std::string a_str = inside.substr(0, dots);
    const std::string b_str = inside.substr(dots + 2);
    int a = 0;
    int b = 0;
    if (!ParseIntToken(a_str, a) || !ParseIntToken(b_str, b)) {
        return {};
    }
    size_t width = 0;
    if ((a_str.size() > 1 && a_str[0] == '0') || (b_str.size() > 1 && b_str[0] == '0')) {
        width = std::max(a_str.size(), b_str.size());
    }

    const int lo = std::min(a, b);
    const int hi = std::max(a, b);
    std::vector<std::string> out;
    out.reserve(static_cast<size_t>(hi - lo + 1));
    for (int i = lo; i <= hi; ++i) {
        std::string num = std::to_string(i);
        if (width > num.size()) {
            num.insert(0, width - num.size(), '0');
        }
        out.push_back(prefix + num);
    }
    return out;
}

void TrimInPlace(std::string& s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.erase(s.begin());
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
}

std::vector<std::string> SplitGroupMembers(const std::string& interior) {
    std::vector<std::string> parts;
    std::string cur;
    int depth = 0;
    for (char c : interior) {
        if (c == '[' || c == '{') {
            ++depth;
            cur.push_back(c);
        } else if (c == ']' || c == '}') {
            --depth;
            cur.push_back(c);
        } else if (depth == 0 &&
                   (c == ',' || std::isspace(static_cast<unsigned char>(c)))) {
            // KiCad group buses allow space and/or comma separators:
            //   SPI{SCK MOSI MISO}  or  SPI{SCK, MOSI, MISO}
            TrimInPlace(cur);
            if (!cur.empty()) {
                parts.push_back(cur);
                cur.clear();
            }
        } else {
            cur.push_back(c);
        }
    }
    TrimInPlace(cur);
    if (!cur.empty()) {
        parts.push_back(cur);
    }
    return parts;
}

void AppendPrefixed(std::vector<std::string>& out,
                    const std::string& prefix,
                    std::vector<std::string> members) {
    for (auto& m : members) {
        if (!prefix.empty()) {
            out.push_back(prefix + "." + m);
        } else {
            out.push_back(std::move(m));
        }
    }
}

std::vector<std::string> ExpandGroup(const std::string& name, const BusAliasMap& aliases);

std::vector<std::string> ExpandToken(const std::string& tok, const BusAliasMap& aliases) {
    auto vect = ExpandVector(tok);
    if (!vect.empty()) {
        return vect;
    }
    if (ParseBusGroup(tok, nullptr, nullptr)) {
        return ExpandGroup(tok, aliases);
    }
    auto ait = aliases.find(tok);
    if (ait != aliases.end()) {
        std::vector<std::string> out;
        for (const auto& mem : ait->second) {
            auto nested = ExpandToken(mem, aliases);
            if (!nested.empty()) {
                out.insert(out.end(), nested.begin(), nested.end());
            } else {
                out.push_back(mem);
            }
        }
        return out;
    }
    return {tok};
}

std::vector<std::string> ExpandGroup(const std::string& name, const BusAliasMap& aliases) {
    std::string group_prefix;
    std::string interior;
    if (!ParseBusGroup(name, &group_prefix, &interior)) {
        return {};
    }
    TrimInPlace(group_prefix);
    const auto tokens = SplitGroupMembers(interior);
    std::vector<std::string> out;
    for (const auto& tok : tokens) {
        AppendPrefixed(out, group_prefix, ExpandToken(tok, aliases));
    }
    return out;
}

}  // namespace

bool ParseBusGroup(const std::string& name, std::string* group_name,
                   std::string* interior) {
    if (name.empty() || name.back() != '}') {
        return false;
    }
    int depth = 0;
    for (size_t i = name.size(); i-- > 0;) {
        if (name[i] == '}') {
            ++depth;
            continue;
        }
        if (name[i] != '{' || --depth != 0) {
            continue;
        }
        // ~{} / _{} / ^{} are text markup: the braces belong to the name.
        if (i > 0 && (name[i - 1] == '~' || name[i - 1] == '_' || name[i - 1] == '^')) {
            return false;
        }
        if (group_name != nullptr) {
            *group_name = name.substr(0, i);
        }
        if (interior != nullptr) {
            *interior = name.substr(i + 1, name.size() - i - 2);
        }
        return true;
    }
    return false;
}

bool IsBusName(const std::string& name) {
    return ParseBusGroup(name, nullptr, nullptr) || !ExpandVector(name).empty();
}

std::vector<std::string> ExpandBusMembers(const std::string& name) {
    static const BusAliasMap kEmpty;
    return ExpandBusMembers(name, kEmpty);
}

std::vector<std::string> ExpandBusMembers(const std::string& name,
                                          const BusAliasMap& aliases) {
    if (name.empty()) {
        return {};
    }
    if (ParseBusGroup(name, nullptr, nullptr)) {
        auto g = ExpandGroup(name, aliases);
        if (!g.empty()) {
            return g;
        }
    }
    if (name.find('[') != std::string::npos) {
        auto v = ExpandVector(name);
        if (!v.empty()) {
            return v;
        }
        return {};
    }
    return {name};
}

BusAliasMap LoadBusAliasesFromProject(const std::string& entry_schematic_path) {
    BusAliasMap out;
    if (entry_schematic_path.empty()) {
        return out;
    }
    fs::path sch(entry_schematic_path);
    fs::path pro = sch;
    pro.replace_extension(".kicad_pro");
    if (!fs::exists(pro)) {
        // entry may be foo.kicad_sch next to ProjectName.kicad_pro with different stem
        fs::path dir = sch.parent_path();
        for (const auto& ent : fs::directory_iterator(dir)) {
            if (ent.path().extension() == ".kicad_pro") {
                pro = ent.path();
                break;
            }
        }
    }
    if (!fs::exists(pro)) {
        return out;
    }

    std::ifstream in(pro, std::ios::binary);
    if (!in) {
        return out;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();

    const std::string key = "\"bus_aliases\"";
    const auto key_pos = text.find(key);
    if (key_pos == std::string::npos) {
        return out;
    }
    const auto brace = text.find('{', key_pos + key.size());
    if (brace == std::string::npos) {
        return out;
    }

    // Scan alias objects: "NAME": [ "a", "b", ... ]
    size_t i = brace + 1;
    while (i < text.size()) {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) {
            ++i;
        }
        if (i >= text.size() || text[i] == '}') {
            break;
        }
        if (text[i] != '"') {
            ++i;
            continue;
        }
        ++i;
        std::string alias_name;
        while (i < text.size() && text[i] != '"') {
            if (text[i] == '\\' && i + 1 < text.size()) {
                alias_name.push_back(text[i + 1]);
                i += 2;
                continue;
            }
            alias_name.push_back(text[i++]);
        }
        if (i < text.size() && text[i] == '"') {
            ++i;
        }
        while (i < text.size() && text[i] != '[') {
            if (text[i] == '}') {
                break;
            }
            ++i;
        }
        if (i >= text.size() || text[i] != '[') {
            break;
        }
        ++i;
        std::vector<std::string> members;
        while (i < text.size() && text[i] != ']') {
            if (text[i] == '"') {
                ++i;
                std::string mem;
                while (i < text.size() && text[i] != '"') {
                    if (text[i] == '\\' && i + 1 < text.size()) {
                        mem.push_back(text[i + 1]);
                        i += 2;
                        continue;
                    }
                    mem.push_back(text[i++]);
                }
                if (i < text.size() && text[i] == '"') {
                    ++i;
                }
                members.push_back(std::move(mem));
            } else {
                ++i;
            }
        }
        if (i < text.size() && text[i] == ']') {
            ++i;
        }
        if (!alias_name.empty()) {
            out[alias_name] = std::move(members);
        }
        while (i < text.size() && (std::isspace(static_cast<unsigned char>(text[i])) || text[i] == ',')) {
            ++i;
        }
    }
    return out;
}

}  // namespace netdiff
