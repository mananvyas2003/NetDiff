#include "json_emit.hpp"

#include <cstdio>
#include <iomanip>

namespace netdiff {
namespace json {

// Locale-independent, cross-stdlib float emission so goldens match on
// libstdc++ / libc++ / MSVC (default ostream<< formatting does not).
void EmitDouble(std::ostringstream& out, double value) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.15g", value);
    out << buf;
}

std::string Escape(const std::string& input) {
    std::ostringstream ss;
    for (unsigned char c : input) {
        switch (c) {
        case '\\': ss << "\\\\"; break;
        case '"':  ss << "\\\""; break;
        case '\b': ss << "\\b"; break;
        case '\f': ss << "\\f"; break;
        case '\n': ss << "\\n"; break;
        case '\r': ss << "\\r"; break;
        case '\t': ss << "\\t"; break;
        default:
            if (c < 0x20) {
                ss << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                   << static_cast<int>(c) << std::dec;
            } else {
                ss << static_cast<char>(c);
            }
            break;
        }
    }
    return ss.str();
}

void EmitString(std::ostringstream& out, const std::string& value) {
    out << '"' << Escape(value) << '"';
}

void EmitBool(std::ostringstream& out, bool value) {
    out << (value ? "true" : "false");
}

void EmitComponent(std::ostringstream& out, const Component& component,
                   const std::string& indent) {
    const std::string f = indent + "  ";
    const std::string f2 = f + "  ";
    const std::string f3 = f2 + "  ";
    out << "{\n";
    out << f << "\"footprint\": "; EmitString(out, component.footprint); out << ",\n";
    out << f << "\"lib_id\": "; EmitString(out, component.lib_id); out << ",\n";
    out << f << "\"pins\": [\n";
    for (size_t p = 0; p < component.pins.size(); ++p) {
        const auto& pin = component.pins[p];
        out << f2 << "{\n";
        out << f3 << "\"component_ref\": "; EmitString(out, pin.component_ref); out << ",\n";
        out << f3 << "\"name\": "; EmitString(out, pin.name); out << ",\n";
        out << f3 << "\"number\": "; EmitString(out, pin.number); out << ",\n";
        out << f3 << "\"unit\": " << pin.unit << "\n";
        out << f2 << "}";
        if (p + 1 < component.pins.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << f << "],\n";
    out << f << "\"position\": {\n";
    out << f2 << "\"rotation\": "; EmitDouble(out, component.rotation); out << ",\n";
    out << f2 << "\"x\": "; EmitDouble(out, component.x); out << ",\n";
    out << f2 << "\"y\": "; EmitDouble(out, component.y); out << "\n";
    out << f << "},\n";
    out << f << "\"ref\": "; EmitString(out, component.ref); out << ",\n";
    out << f << "\"sheet_path\": "; EmitString(out, component.sheet_path); out << ",\n";
    out << f << "\"value\": "; EmitString(out, component.value); out << "\n";
    out << indent << "}";
}

void EmitNet(std::ostringstream& out, const Net& net, const std::string& indent) {
    const std::string f = indent + "  ";
    const std::string f2 = f + "  ";
    out << "{\n";
    out << f << "\"is_named\": "; EmitBool(out, net.is_named); out << ",\n";
    out << f << "\"is_power\": "; EmitBool(out, net.is_power); out << ",\n";
    out << f << "\"name\": "; EmitString(out, net.name); out << ",\n";
    out << f << "\"net_id\": "; EmitString(out, net.net_id); out << ",\n";
    out << f << "\"pins\": [\n";
    for (size_t p = 0; p < net.pins.size(); ++p) {
        out << f2; EmitString(out, net.pins[p]);
        if (p + 1 < net.pins.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << f << "],\n";
    out << f << "\"sheet_scope\": "; EmitString(out, net.sheet_scope); out << "\n";
    out << indent << "}";
}

}  // namespace json
}  // namespace netdiff
