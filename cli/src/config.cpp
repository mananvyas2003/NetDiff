#include "config.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace netdiff {
namespace cli {
namespace {

namespace fs = std::filesystem;

// A deliberately small YAML reader. `.netdiff.yml` is a fixed, shallow shape
// (02 §4), so parsing exactly that subset beats taking on a YAML dependency for
// a handful of keys — see AGENTS.md on justifying new dependencies. Supported:
// comments, `key: value`, one level of nesting by indentation, `key: []`,
// block sequences (`- item`) and inline sequences (`[a, b]`).

std::string Trim(const std::string& text) {
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return std::string();
    }
    const auto end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

std::string StripQuotes(const std::string& text) {
    if (text.size() >= 2 && ((text.front() == '"' && text.back() == '"') ||
                             (text.front() == '\'' && text.back() == '\''))) {
        return text.substr(1, text.size() - 2);
    }
    return text;
}

// Drop a trailing `# comment`, honouring quotes so a '#' inside a name stays.
std::string StripComment(const std::string& line) {
    bool in_single = false;
    bool in_double = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '\'' && !in_double) {
            in_single = !in_single;
        } else if (c == '"' && !in_single) {
            in_double = !in_double;
        } else if (c == '#' && !in_single && !in_double) {
            return line.substr(0, i);
        }
    }
    return line;
}

std::vector<std::string> SplitInlineList(const std::string& text) {
    std::vector<std::string> items;
    std::string inner = Trim(text);
    if (inner.size() >= 2 && inner.front() == '[' && inner.back() == ']') {
        inner = inner.substr(1, inner.size() - 2);
    }
    std::string current;
    for (char c : inner) {
        if (c == ',') {
            const std::string item = StripQuotes(Trim(current));
            if (!item.empty()) {
                items.push_back(item);
            }
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    const std::string item = StripQuotes(Trim(current));
    if (!item.empty()) {
        items.push_back(item);
    }
    return items;
}

bool ParseBool(const std::string& text, bool* value) {
    const std::string lowered = Trim(text);
    if (lowered == "true" || lowered == "yes" || lowered == "on") {
        *value = true;
        return true;
    }
    if (lowered == "false" || lowered == "no" || lowered == "off") {
        *value = false;
        return true;
    }
    return false;
}

bool ParseDouble(const std::string& text, double* value) {
    try {
        size_t consumed = 0;
        const double parsed = std::stod(Trim(text), &consumed);
        if (consumed == 0) {
            return false;
        }
        *value = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

size_t IndentOf(const std::string& line) {
    size_t indent = 0;
    while (indent < line.size() && (line[indent] == ' ' || line[indent] == '\t')) {
        ++indent;
    }
    return indent;
}

}  // namespace

bool ParseFailOn(const std::string& text, DiffConfig::Gate::FailOn* fail_on) {
    if (text == "significant") {
        *fail_on = DiffConfig::Gate::FailOn::kSignificant;
    } else if (text == "any") {
        *fail_on = DiffConfig::Gate::FailOn::kAny;
    } else if (text == "never") {
        *fail_on = DiffConfig::Gate::FailOn::kNever;
    } else {
        return false;
    }
    return true;
}

ConfigLoadResult ParseConfig(const std::string& text, const std::string& path) {
    ConfigLoadResult result;
    result.path = path;

    std::istringstream stream(text);
    std::string raw;
    std::string section;      // current top-level key
    std::string list_key;     // key whose block sequence we are reading
    std::vector<std::string> lines;
    while (std::getline(stream, raw)) {
        lines.push_back(raw);
    }

    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string line = StripComment(lines[i]);
        const std::string body = Trim(line);
        if (body.empty()) {
            continue;
        }
        const size_t indent = IndentOf(line);

        if (body[0] == '-') {
            // Block sequence item belonging to `section.list_key`.
            const std::string item = Trim(body.substr(1));
            if (list_key.empty()) {
                result.warnings.push_back("list item outside a known key: " + body);
                continue;
            }
            if (section == "net_normalization" && list_key == "aliases") {
                std::vector<std::string> group = SplitInlineList(item);
                if (group.size() < 2) {
                    result.warnings.push_back("alias group needs 2+ names: " + item);
                } else {
                    result.config.net_normalization.aliases.push_back(std::move(group));
                }
            } else if (section == "gate" && list_key == "ignore_change_types") {
                result.config.gate.ignore_change_types.push_back(StripQuotes(item));
            } else if (section == "ignore" && list_key == "components") {
                result.config.ignore.components.push_back(StripQuotes(item));
            } else if (section == "ignore" && list_key == "nets") {
                result.config.ignore.nets.push_back(StripQuotes(item));
            } else {
                result.warnings.push_back("unsupported list: " + section + "." + list_key);
            }
            continue;
        }

        const auto colon = body.find(':');
        if (colon == std::string::npos) {
            result.warnings.push_back("ignored line: " + body);
            continue;
        }
        const std::string key = Trim(body.substr(0, colon));
        const std::string value = Trim(body.substr(colon + 1));

        if (indent == 0) {
            section = key;
            list_key.clear();
            if (key == "schema_version") {
                const std::string version = StripQuotes(value);
                if (!version.empty() && version.rfind("1", 0) != 0) {
                    result.ok = false;
                    result.error = "unsupported config schema_version '" + version +
                                   "' (this build understands 1.x)";
                    return result;
                }
            } else if (key != "gate" && key != "net_normalization" && key != "ignore" &&
                       key != "unnamed_net_matching") {
                result.warnings.push_back("unknown config section: " + key);
            }
            continue;
        }

        // Nested key. An empty value introduces a block sequence.
        if (value.empty()) {
            list_key = key;
            continue;
        }
        list_key.clear();

        if (section == "gate" && key == "fail_on") {
            if (!ParseFailOn(StripQuotes(value), &result.config.gate.fail_on)) {
                result.ok = false;
                result.error = "gate.fail_on must be significant|any|never, got '" + value + "'";
                return result;
            }
        } else if (section == "gate" && key == "ignore_change_types") {
            result.config.gate.ignore_change_types = SplitInlineList(value);
        } else if (section == "net_normalization" && key == "case_insensitive") {
            if (!ParseBool(value, &result.config.net_normalization.case_insensitive)) {
                result.ok = false;
                result.error = "net_normalization.case_insensitive must be true|false";
                return result;
            }
        } else if (section == "net_normalization" && key == "aliases") {
            if (Trim(value) != "[]") {
                result.warnings.push_back("net_normalization.aliases must be a list of pairs");
            }
        } else if (section == "ignore" && key == "components") {
            result.config.ignore.components = SplitInlineList(value);
        } else if (section == "ignore" && key == "nets") {
            result.config.ignore.nets = SplitInlineList(value);
        } else if (section == "unnamed_net_matching" && key == "jaccard_threshold") {
            double threshold = 0.0;
            if (!ParseDouble(value, &threshold) || threshold < 0.0 || threshold > 1.0) {
                result.ok = false;
                result.error =
                    "unnamed_net_matching.jaccard_threshold must be between 0 and 1, got '" +
                    value + "'";
                return result;
            }
            result.config.unnamed_net_matching.jaccard_threshold = threshold;
        } else {
            result.warnings.push_back("unknown config key: " + section + "." + key);
        }
    }
    return result;
}

ConfigLoadResult LoadConfig(const std::string& explicit_path, const std::string& start_dir) {
    if (!explicit_path.empty()) {
        std::ifstream in(explicit_path, std::ios::binary);
        if (!in) {
            ConfigLoadResult result;
            result.ok = false;
            result.error = "cannot open config file: " + explicit_path;
            return result;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        return ParseConfig(ss.str(), explicit_path);
    }

    // Walk upwards looking for .netdiff.yml (04 §4).
    std::error_code ec;
    fs::path dir = fs::absolute(start_dir.empty() ? fs::current_path(ec) : fs::path(start_dir), ec);
    while (!dir.empty()) {
        const fs::path candidate = dir / ".netdiff.yml";
        if (fs::exists(candidate, ec)) {
            std::ifstream in(candidate, std::ios::binary);
            if (in) {
                std::ostringstream ss;
                ss << in.rdbuf();
                return ParseConfig(ss.str(), candidate.string());
            }
        }
        const fs::path parent = dir.parent_path();
        if (parent == dir) {
            break;
        }
        dir = parent;
    }
    return ConfigLoadResult{};  // built-in defaults
}

}  // namespace cli
}  // namespace netdiff
