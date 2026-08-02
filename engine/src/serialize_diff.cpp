#include "netdiff/diff.hpp"

#include <iomanip>
#include <sstream>
#include <string>
#include <variant>

#include "json_emit.hpp"

namespace netdiff {
namespace {

void EmitStringArray(std::ostringstream& out, const std::vector<std::string>& values,
                     const std::string& indent) {
    out << "[\n";
    for (size_t i = 0; i < values.size(); ++i) {
        out << indent << "  "; json::EmitString(out, values[i]);
        if (i + 1 < values.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << indent << "]";
}

// Writes the payload object. Field keys are lexicographic within each payload
// type, matching 02_DATA_MODEL.md §3.2.
struct PayloadEmitter {
    std::ostringstream& out;
    std::string indent;  // indentation of the payload object's braces

    std::string Field() const { return indent + "  "; }

    void operator()(const ComponentAdded& p) const {
        out << "{\n" << Field() << "\"component\": ";
        json::EmitComponent(out, p.component, Field());
        out << "\n" << indent << "}";
    }

    void operator()(const ComponentRemoved& p) const {
        out << "{\n" << Field() << "\"component\": ";
        json::EmitComponent(out, p.component, Field());
        out << "\n" << indent << "}";
    }

    void operator()(const ComponentModified& p) const {
        const std::string f = Field();
        const std::string f2 = f + "  ";
        const std::string f3 = f2 + "  ";
        out << "{\n" << f << "\"changes\": [\n";
        for (size_t i = 0; i < p.changes.size(); ++i) {
            const auto& fc = p.changes[i];
            out << f2 << "{\n";
            out << f3 << "\"after\": "; json::EmitString(out, fc.after); out << ",\n";
            out << f3 << "\"before\": "; json::EmitString(out, fc.before); out << ",\n";
            out << f3 << "\"field\": "; json::EmitString(out, fc.field); out << "\n";
            out << f2 << "}";
            if (i + 1 < p.changes.size()) {
                out << ",";
            }
            out << "\n";
        }
        out << f << "],\n";
        out << f << "\"ref\": "; json::EmitString(out, p.ref); out << "\n";
        out << indent << "}";
    }

    void operator()(const NetAdded& p) const {
        out << "{\n" << Field() << "\"net\": ";
        json::EmitNet(out, p.net, Field());
        out << "\n" << indent << "}";
    }

    void operator()(const NetRemoved& p) const {
        out << "{\n" << Field() << "\"net\": ";
        json::EmitNet(out, p.net, Field());
        out << "\n" << indent << "}";
    }

    void operator()(const NetRenamed& p) const {
        const std::string f = Field();
        out << "{\n";
        out << f << "\"after_name\": "; json::EmitString(out, p.after_name); out << ",\n";
        out << f << "\"before_name\": "; json::EmitString(out, p.before_name); out << ",\n";
        out << f << "\"net\": ";
        json::EmitNet(out, p.net, f);
        out << "\n" << indent << "}";
    }

    void operator()(const NetMerged& p) const {
        const std::string f = Field();
        out << "{\n";
        out << f << "\"after_net\": "; json::EmitString(out, p.after_net); out << ",\n";
        out << f << "\"before_nets\": "; EmitStringArray(out, p.before_nets, f); out << ",\n";
        out << f << "\"pins_involved\": "; EmitStringArray(out, p.pins_involved, f);
        out << "\n" << indent << "}";
    }

    void operator()(const NetSplit& p) const {
        const std::string f = Field();
        out << "{\n";
        out << f << "\"after_nets\": "; EmitStringArray(out, p.after_nets, f); out << ",\n";
        out << f << "\"before_net\": "; json::EmitString(out, p.before_net); out << ",\n";
        out << f << "\"pins_involved\": "; EmitStringArray(out, p.pins_involved, f);
        out << "\n" << indent << "}";
    }

    void operator()(const PinConnectionChanged& p) const {
        const std::string f = Field();
        out << "{\n";
        out << f << "\"after_net\": "; json::EmitString(out, p.after_net); out << ",\n";
        out << f << "\"before_net\": "; json::EmitString(out, p.before_net); out << ",\n";
        out << f << "\"pin\": "; json::EmitString(out, p.pin); out << "\n";
        out << indent << "}";
    }
};

}  // namespace

std::string SerializeDiffJson(const DiffResult& result) {
    std::ostringstream out;
    out << std::setprecision(17);

    out << "{\n";
    out << "  \"after_ref\": "; json::EmitString(out, result.after_ref); out << ",\n";
    out << "  \"before_ref\": "; json::EmitString(out, result.before_ref); out << ",\n";

    out << "  \"changes\": [\n";
    for (size_t i = 0; i < result.changes.size(); ++i) {
        const auto& change = result.changes[i];
        out << "    {\n";
        out << "      \"critical\": "; json::EmitBool(out, change.critical); out << ",\n";
        out << "      \"message\": "; json::EmitString(out, change.message); out << ",\n";
        out << "      \"payload\": ";
        std::visit(PayloadEmitter{out, "      "}, change.payload);
        out << ",\n";
        out << "      \"significance\": ";
        json::EmitString(out, ToString(change.significance));
        out << ",\n";
        out << "      \"type\": "; json::EmitString(out, ToString(change.type())); out << "\n";
        out << "    }";
        if (i + 1 < result.changes.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ],\n";

    out << "  \"schema_version\": "; json::EmitString(out, result.schema_version); out << ",\n";

    out << "  \"summary\": {\n";
    out << "    \"by_type\": {\n";
    {
        size_t i = 0;
        for (const auto& kv : result.summary.by_type) {
            out << "      "; json::EmitString(out, kv.first);
            out << ": " << kv.second;
            if (++i < result.summary.by_type.size()) {
                out << ",";
            }
            out << "\n";
        }
    }
    out << "    },\n";
    out << "    \"cosmetic_count\": " << result.summary.cosmetic_count << ",\n";
    out << "    \"gate\": "; json::EmitString(out, ToString(result.summary.gate)); out << ",\n";
    out << "    \"significant_count\": " << result.summary.significant_count << "\n";
    out << "  }\n";
    out << "}\n";

    return out.str();
}

}  // namespace netdiff
