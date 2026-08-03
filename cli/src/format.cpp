#include "format.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace netdiff {
namespace cli {
namespace {

// ---- shared helpers --------------------------------------------------------

std::string JsonEscape(const std::string& input) {
    std::ostringstream out;
    for (unsigned char c : input) {
        switch (c) {
        case '\\': out << "\\\\"; break;
        case '"':  out << "\\\""; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (c < 0x20) {
                static const char* kHex = "0123456789abcdef";
                out << "\\u00" << kHex[(c >> 4) & 0xF] << kHex[c & 0xF];
            } else {
                out << static_cast<char>(c);
            }
            break;
        }
    }
    return out.str();
}

// A change is only shown when it is significant, or the caller asked for the
// cosmetic ones too (04 §1.4: cosmetic hidden in text unless --include-cosmetic).
bool Visible(const Change& change, const FormatOptions& options) {
    return change.significance == Significance::kSignificant || options.include_cosmetic;
}

// Markdown table cells must not break the table.
std::string EscapeMarkdown(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        if (c == '|') {
            out += "\\|";
        } else if (c == '\n') {
            out += ' ';
        } else {
            out.push_back(c);
        }
    }
    return out;
}

// The subject the change is about: a refdes, a PinId, or a net name. Used for
// grouping in text/markdown and for the SARIF logical location.
std::string SubjectOf(const Change& change) {
    struct Visitor {
        std::string operator()(const ComponentAdded& p) const { return p.component.ref; }
        std::string operator()(const ComponentRemoved& p) const { return p.component.ref; }
        std::string operator()(const ComponentModified& p) const { return p.ref; }
        std::string operator()(const NetAdded& p) const { return p.net.name; }
        std::string operator()(const NetRemoved& p) const { return p.net.name; }
        std::string operator()(const NetRenamed& p) const { return p.after_name; }
        std::string operator()(const NetMerged& p) const { return p.after_net; }
        std::string operator()(const NetSplit& p) const { return p.before_net; }
        std::string operator()(const PinConnectionChanged& p) const { return p.pin; }
    };
    return std::visit(Visitor{}, change.payload);
}

// Broad buckets for human output, in the order a reviewer wants them.
enum class Section { kCritical, kPins, kNets, kComponents, kCosmetic };

Section SectionOf(const Change& change) {
    if (change.significance == Significance::kCosmetic) {
        return Section::kCosmetic;
    }
    if (change.critical) {
        return Section::kCritical;
    }
    switch (change.type()) {
    case ChangeType::kPinConnectionChanged:
        return Section::kPins;
    case ChangeType::kComponentAdded:
    case ChangeType::kComponentRemoved:
    case ChangeType::kComponentModified:
        return Section::kComponents;
    default:
        return Section::kNets;
    }
}

const char* SectionTitle(Section section) {
    switch (section) {
    case Section::kCritical:   return "Critical";
    case Section::kPins:       return "Pin connections";
    case Section::kNets:       return "Nets";
    case Section::kComponents: return "Components";
    case Section::kCosmetic:   return "Cosmetic";
    }
    return "Other";
}

const std::vector<Section>& AllSections() {
    static const std::vector<Section> kSections = {Section::kCritical, Section::kPins,
                                                   Section::kNets, Section::kComponents,
                                                   Section::kCosmetic};
    return kSections;
}

// ---- text ------------------------------------------------------------------

struct Palette {
    const char* reset = "";
    const char* bold = "";
    const char* red = "";
    const char* yellow = "";
    const char* green = "";
    const char* dim = "";
};

Palette PaletteFor(bool color) {
    Palette p;
    if (color) {
        p.reset = "\033[0m";
        p.bold = "\033[1m";
        p.red = "\033[31m";
        p.yellow = "\033[33m";
        p.green = "\033[32m";
        p.dim = "\033[2m";
    }
    return p;
}

const char* BulletFor(const Change& change) {
    if (change.critical) {
        return "!!";
    }
    switch (change.type()) {
    case ChangeType::kComponentAdded:
    case ChangeType::kNetAdded:
        return " +";
    case ChangeType::kComponentRemoved:
    case ChangeType::kNetRemoved:
        return " -";
    case ChangeType::kPinConnectionChanged:
        return " >";
    default:
        return " ~";
    }
}

std::string FormatText(const DiffResult& result, const FormatOptions& options) {
    const Palette palette = PaletteFor(options.color);
    const bool failed = result.summary.gate == GateResult::kFail;
    std::ostringstream out;

    out << palette.bold << "netdiff" << palette.reset << " "
        << (result.before_ref.empty() ? "<before>" : result.before_ref) << " -> "
        << (result.after_ref.empty() ? "<after>" : result.after_ref) << "\n";
    out << "  " << result.summary.significant_count << " significant, "
        << result.summary.cosmetic_count << " cosmetic";
    out << "   gate: " << (failed ? palette.red : palette.green)
        << ToString(result.summary.gate) << palette.reset << "\n";

    size_t shown = 0;
    for (Section section : AllSections()) {
        std::vector<const Change*> bucket;
        for (const auto& change : result.changes) {
            if (Visible(change, options) && SectionOf(change) == section) {
                bucket.push_back(&change);
            }
        }
        if (bucket.empty()) {
            continue;
        }
        const char* color = palette.reset;
        if (section == Section::kCritical) {
            color = palette.red;
        } else if (section == Section::kCosmetic) {
            color = palette.dim;
        } else {
            color = palette.yellow;
        }
        out << "\n" << palette.bold << SectionTitle(section) << palette.reset << "\n";
        for (const Change* change : bucket) {
            out << "  " << color << BulletFor(*change) << palette.reset << " "
                << change->message << "\n";
            ++shown;
        }
    }

    if (shown == 0) {
        out << "\n  No electrical changes.\n";
        if (result.summary.cosmetic_count > 0) {
            out << "  " << palette.dim << "(" << result.summary.cosmetic_count
                << " cosmetic change(s) hidden; pass --include-cosmetic to show them)"
                << palette.reset << "\n";
        }
    }
    return out.str();
}

// ---- markdown --------------------------------------------------------------

std::string FormatMarkdown(const DiffResult& result, const FormatOptions& options) {
    std::ostringstream out;
    const bool failed = result.summary.gate == GateResult::kFail;

    out << "### NetDiff — " << (failed ? "**FAIL**" : "**PASS**") << "\n\n";
    out << "`" << EscapeMarkdown(result.before_ref.empty() ? "<before>" : result.before_ref)
        << "` → `" << EscapeMarkdown(result.after_ref.empty() ? "<after>" : result.after_ref)
        << "`\n\n";
    out << result.summary.significant_count << " significant, "
        << result.summary.cosmetic_count << " cosmetic\n\n";

    if (!result.summary.by_type.empty()) {
        out << "| Change type | Count |\n|---|---:|\n";
        for (const auto& entry : result.summary.by_type) {
            out << "| " << EscapeMarkdown(entry.first) << " | " << entry.second << " |\n";
        }
        out << "\n";
    }

    size_t shown = 0;
    for (Section section : AllSections()) {
        std::vector<const Change*> bucket;
        for (const auto& change : result.changes) {
            if (Visible(change, options) && SectionOf(change) == section) {
                bucket.push_back(&change);
            }
        }
        if (bucket.empty()) {
            continue;
        }
        out << "**" << SectionTitle(section) << "**\n\n";
        for (const Change* change : bucket) {
            out << "- " << (change->critical ? "⚠️ " : "") << EscapeMarkdown(change->message)
                << "\n";
            ++shown;
        }
        out << "\n";
    }
    if (shown == 0) {
        out << "No electrical changes.\n";
    }
    return out.str();
}

// ---- SARIF 2.1.0 -----------------------------------------------------------

// error for a critical change, warning for the rest of the significant ones,
// note for cosmetic (04 §1.4: level comes from significance).
const char* SarifLevel(const Change& change) {
    if (change.critical) {
        return "error";
    }
    return change.significance == Significance::kSignificant ? "warning" : "note";
}

std::string SarifUri(const std::string& path) {
    std::string uri = path;
    std::replace(uri.begin(), uri.end(), '\\', '/');
    return uri;
}

std::string FormatSarif(const DiffResult& result, const FormatOptions& options) {
    std::vector<const Change*> results;
    for (const auto& change : result.changes) {
        if (Visible(change, options)) {
            results.push_back(&change);
        }
    }

    // Only the rules actually referenced, ordered for byte-stability.
    std::map<std::string, ChangeType> rules;
    for (const Change* change : results) {
        rules.emplace(RuleIdFor(change->type()), change->type());
    }

    std::ostringstream out;
    out << "{\n";
    out << "  \"$schema\": \"https://json.schemastore.org/sarif-2.1.0.json\",\n";
    out << "  \"runs\": [\n";
    out << "    {\n";
    out << "      \"results\": [\n";
    for (size_t i = 0; i < results.size(); ++i) {
        const Change& change = *results[i];
        out << "        {\n";
        out << "          \"level\": \"" << SarifLevel(change) << "\",\n";
        out << "          \"locations\": [\n";
        out << "            {\n";
        if (!options.artifact_uri.empty()) {
            out << "              \"physicalLocation\": {\n";
            out << "                \"artifactLocation\": {\n";
            out << "                  \"uri\": \"" << JsonEscape(SarifUri(options.artifact_uri))
                << "\"\n";
            out << "                }\n";
            out << "              },\n";
        }
        out << "              \"logicalLocations\": [\n";
        out << "                {\n";
        out << "                  \"kind\": \""
            << (change.type() == ChangeType::kPinConnectionChanged ? "member" : "module")
            << "\",\n";
        out << "                  \"name\": \"" << JsonEscape(SubjectOf(change)) << "\"\n";
        out << "                }\n";
        out << "              ]\n";
        out << "            }\n";
        out << "          ],\n";
        out << "          \"message\": {\n";
        out << "            \"text\": \"" << JsonEscape(change.message) << "\"\n";
        out << "          },\n";
        out << "          \"ruleId\": \"" << RuleIdFor(change.type()) << "\"\n";
        out << "        }";
        if (i + 1 < results.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "      ],\n";
    out << "      \"tool\": {\n";
    out << "        \"driver\": {\n";
    out << "          \"informationUri\": \"https://github.com/netdiff/netdiff\",\n";
    out << "          \"name\": \"netdiff\",\n";
    out << "          \"rules\": [\n";
    {
        size_t i = 0;
        for (const auto& entry : rules) {
            out << "            {\n";
            out << "              \"id\": \"" << entry.first << "\",\n";
            out << "              \"name\": \"" << ToString(entry.second) << "\",\n";
            out << "              \"shortDescription\": {\n";
            out << "                \"text\": \"" << ToString(entry.second)
                << " detected by semantic schematic diff\"\n";
            out << "              }\n";
            out << "            }";
            if (++i < rules.size()) {
                out << ",";
            }
            out << "\n";
        }
    }
    out << "          ],\n";
    out << "          \"version\": \"" << JsonEscape(options.tool_version) << "\"\n";
    out << "        }\n";
    out << "      }\n";
    out << "    }\n";
    out << "  ],\n";
    out << "  \"version\": \"2.1.0\"\n";
    out << "}\n";
    return out.str();
}

}  // namespace

std::string RuleIdFor(ChangeType type) {
    const std::string name = ToString(type);
    std::string id;
    for (size_t i = 0; i < name.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if (std::isupper(c) != 0) {
            if (i != 0) {
                id.push_back('-');
            }
            id.push_back(static_cast<char>(std::tolower(c)));
        } else {
            id.push_back(name[i]);
        }
    }
    return id;
}

bool ParseOutputFormat(const std::string& text, OutputFormat* format) {
    if (text == "text") {
        *format = OutputFormat::kText;
    } else if (text == "json") {
        *format = OutputFormat::kJson;
    } else if (text == "markdown") {
        *format = OutputFormat::kMarkdown;
    } else if (text == "sarif") {
        *format = OutputFormat::kSarif;
    } else if (text == "html") {
        *format = OutputFormat::kHtml;
    } else {
        return false;
    }
    return true;
}

std::string FormatDiff(const DiffResult& result, OutputFormat format,
                       const FormatOptions& options) {
    switch (format) {
    case OutputFormat::kJson:
        // The full DiffResult, unfiltered: tooling wants everything (04 §1.4).
        return SerializeDiffJson(result);
    case OutputFormat::kMarkdown:
        return FormatMarkdown(result, options);
    case OutputFormat::kSarif:
        return FormatSarif(result, options);
    case OutputFormat::kHtml:
        return FormatHtml(result, options);
    case OutputFormat::kText:
        break;
    }
    return FormatText(result, options);
}

}  // namespace cli
}  // namespace netdiff
