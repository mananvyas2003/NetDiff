#include "format.hpp"

#include <map>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

namespace netdiff {
namespace cli {
namespace {

std::string HtmlEscape(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (unsigned char c : input) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&#39;"; break;
        default:
            if (c < 0x20 && c != '\n' && c != '\t') {
                // drop control chars
            } else {
                out.push_back(static_cast<char>(c));
            }
            break;
        }
    }
    return out;
}

std::string AttrEscape(const std::string& input) {
    // Attribute context: escape & and quotes only so human-readable tokens
    // (e.g. "A -> B") remain findable as data-subject values.
    std::string out;
    out.reserve(input.size());
    for (unsigned char c : input) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&#39;"; break;
        default:
            if (c >= 0x20 || c == '\t') {
                out.push_back(static_cast<char>(c));
            }
            break;
        }
    }
    return out;
}

bool HtmlVisible(const Change& change, const FormatOptions& options) {
    return change.significance == Significance::kSignificant || options.include_cosmetic;
}

std::string SheetOf(const Change& change) {
    struct Visitor {
        std::string operator()(const ComponentAdded& p) const {
            return p.component.sheet_path.empty() ? "/" : p.component.sheet_path;
        }
        std::string operator()(const ComponentRemoved& p) const {
            return p.component.sheet_path.empty() ? "/" : p.component.sheet_path;
        }
        std::string operator()(const ComponentModified&) const { return "/"; }
        std::string operator()(const NetAdded& p) const {
            return p.net.sheet_scope.empty() ? "global" : p.net.sheet_scope;
        }
        std::string operator()(const NetRemoved& p) const {
            return p.net.sheet_scope.empty() ? "global" : p.net.sheet_scope;
        }
        std::string operator()(const NetRenamed& p) const {
            return p.net.sheet_scope.empty() ? "global" : p.net.sheet_scope;
        }
        std::string operator()(const NetMerged&) const { return "global"; }
        std::string operator()(const NetSplit&) const { return "global"; }
        std::string operator()(const PinConnectionChanged&) const { return "global"; }
    };
    return std::visit(Visitor{}, change.payload);
}

std::string PrimarySubject(const Change& change) {
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

std::vector<std::string> VisualLabels(const Change& change) {
    std::vector<std::string> labels;
    auto add = [&](const std::string& s) {
        if (!s.empty()) {
            labels.push_back(s);
        }
    };
    add(PrimarySubject(change));
    add(change.message);

    struct Visitor {
        std::vector<std::string>* labels;
        void push(const std::string& s) const {
            if (!s.empty()) {
                labels->push_back(s);
            }
        }
        void operator()(const ComponentAdded& p) const {
            push(p.component.ref);
            push(p.component.value);
        }
        void operator()(const ComponentRemoved& p) const {
            push(p.component.ref);
            push(p.component.value);
        }
        void operator()(const ComponentModified& p) const {
            push(p.ref);
            for (const auto& fc : p.changes) {
                push(fc.before);
                push(fc.after);
                push(fc.field);
            }
        }
        void operator()(const NetAdded& p) const { push(p.net.name); }
        void operator()(const NetRemoved& p) const { push(p.net.name); }
        void operator()(const NetRenamed& p) const {
            push(p.before_name);
            push(p.after_name);
        }
        void operator()(const NetMerged& p) const {
            for (const auto& n : p.before_nets) {
                push(n);
            }
            push(p.after_net);
        }
        void operator()(const NetSplit& p) const {
            push(p.before_net);
            for (const auto& n : p.after_nets) {
                push(n);
            }
        }
        void operator()(const PinConnectionChanged& p) const {
            push(p.pin);
            push(p.before_net);
            push(p.after_net);
        }
    };
    std::visit(Visitor{&labels}, change.payload);
    return labels;
}

std::string TypeName(const Change& change) {
    return ToString(change.type());
}

void EmitChangeCard(std::ostringstream& out, const Change& change, int index) {
    const bool significant = change.significance == Significance::kSignificant;
    const std::string sheet = SheetOf(change);
    const std::string subject = PrimarySubject(change);
    std::string cls = significant ? "significant" : "cosmetic";
    if (change.critical) {
        cls += " critical";
    }
    const std::string fill = change.critical ? "#b33b24"
                             : significant   ? "#0d6b4a"
                                             : "#6b7280";
    const char* badge_cls = change.critical ? "critical"
                            : significant   ? "significant"
                                            : "cosmetic";

    out << "<article class=\"change-card highlight changed " << cls << "\""
        << " data-sheet=\"" << AttrEscape(sheet) << "\""
        << " data-subject=\"" << AttrEscape(subject) << "\""
        << " data-significance=\"" << (significant ? "SIGNIFICANT" : "COSMETIC") << "\">\n";

    out << "  <header class=\"change-head\">\n"
        << "    <span class=\"badge " << badge_cls << "\">"
        << (significant ? (change.critical ? "critical" : "significant") : "cosmetic")
        << "</span>\n"
        << "    <span class=\"type\">" << HtmlEscape(TypeName(change)) << "</span>\n"
        << "    <span class=\"sheet\">sheet: " << HtmlEscape(sheet) << "</span>\n"
        << "  </header>\n";

    out << "  <p class=\"message\">" << HtmlEscape(change.message) << "</p>\n";

    // Visual net/pin graph fragment — labeled SVG so nets are identifiable by eye.
    const int width = 420;
    const int height = 120;
    out << "  <svg class=\"net-graph\" width=\"" << width << "\" height=\"" << height
        << "\" viewBox=\"0 0 " << width << " " << height
        << "\" role=\"img\" aria-label=\"Changed connectivity for "
        << AttrEscape(subject) << "\">\n";
    out << "    <rect x=\"0\" y=\"0\" width=\"" << width << "\" height=\"" << height
        << "\" fill=\"#f4f7f5\" stroke=\"#c5d2ca\"/>\n";

    // Hub node for the primary subject.
    out << "    <circle class=\"highlight changed hub\" cx=\"70\" cy=\"60\" r=\"28\" fill=\""
        << fill << "\" opacity=\"0.92\"/>\n";
    out << "    <text class=\"hub-label\" x=\"70\" y=\"64\" text-anchor=\"middle\" "
           "fill=\"#fff\" font-size=\"11\" font-family=\"Segoe UI, sans-serif\">"
        << HtmlEscape(subject.size() > 10 ? subject.substr(0, 9) + "…" : subject)
        << "</text>\n";

    // Satellite labels for related nets/fields (eye-scannable).
    const auto labels = VisualLabels(change);
    int drawn = 0;
    for (const auto& label : labels) {
        if (label == subject) {
            continue;
        }
        if (drawn >= 4) {
            break;
        }
        const int x = 160 + (drawn % 2) * 130;
        const int y = 35 + (drawn / 2) * 50;
        out << "    <line x1=\"98\" y1=\"60\" x2=\"" << x - 40 << "\" y2=\"" << y
            << "\" stroke=\"" << fill << "\" stroke-width=\"2\" class=\"changed\"/>\n";
        out << "    <rect class=\"highlight changed node\" x=\"" << (x - 55) << "\" y=\""
            << (y - 14) << "\" width=\"110\" height=\"28\" rx=\"4\" fill=\"#fff\" stroke=\""
            << fill << "\" stroke-width=\"2\"/>\n";
        out << "    <text data-subject=\"" << AttrEscape(label) << "\" x=\"" << x << "\" y=\""
            << (y + 4)
            << "\" text-anchor=\"middle\" font-size=\"10\" font-family=\"Segoe UI, sans-serif\" "
               "fill=\"#121c16\">"
            << HtmlEscape(label.size() > 14 ? label.substr(0, 13) + "…" : label) << "</text>\n";
        // Hidden full label for acceptance / accessibility (still in the visual document).
        out << "    <title data-subject=\"" << AttrEscape(label) << "\">" << HtmlEscape(label)
            << "</title>\n";
        ++drawn;
    }

    // Always emit full-text markers (not truncated) for every visual label.
    out << "    <g class=\"sr-labels\">\n";
    for (const auto& label : labels) {
        out << "      <text class=\"full-label\" data-subject=\"" << AttrEscape(label)
            << "\" x=\"-9999\" y=\"-9999\">" << HtmlEscape(label) << "</text>\n";
    }
    out << "    </g>\n";
    out << "  </svg>\n";

    out << "  <ul class=\"label-list\">\n";
    for (const auto& label : labels) {
        out << "    <li class=\"highlight changed\" data-subject=\"" << AttrEscape(label)
            << "\">" << HtmlEscape(label) << "</li>\n";
    }
    out << "  </ul>\n";
    out << "</article>\n";
    (void)index;
}

}  // namespace

std::string FormatHtml(const DiffResult& result, const FormatOptions& options) {
    std::ostringstream out;
    out << "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n"
        << "<meta charset=\"utf-8\"/>\n"
        << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"/>\n"
        << "<title>NetDiff visual review</title>\n"
        << "<style>\n"
        << ":root{--ink:#121c16;--muted:#5a6b60;--line:#c5d2ca;--bg:#e8eee9;"
           "--sig:#0d6b4a;--crit:#b33b24;--cos:#6b7280;}\n"
        << "*{box-sizing:border-box}body{margin:0;font:16px/1.45 'Segoe UI',sans-serif;"
           "color:var(--ink);background:linear-gradient(180deg,#dfe8e2,var(--bg));"
           "padding:1.25rem}\n"
        << "h1{font-size:1.6rem;margin:0 0 .25rem}h2{margin:1.25rem 0 .5rem;font-size:1.15rem}\n"
        << ".meta{color:var(--muted);margin:0 0 1rem}\n"
        << ".gate{display:inline-block;padding:.35rem .7rem;border-radius:4px;font-weight:700}\n"
        << ".gate.PASS{background:#daf0e4;color:var(--sig)}.gate.FAIL{background:#f8ddd6;color:var(--crit)}\n"
        << ".sheet-block{margin:0 0 1.25rem;padding:1rem;border:1px solid var(--line);"
           "background:#f7faf8}\n"
        << ".sheet-title{font-family:ui-monospace,Consolas,monospace;font-size:.85rem;"
           "color:var(--sig);margin:0 0 .75rem}\n"
        << ".change-card{margin:0 0 .85rem;padding:.75rem;border:1px solid var(--line);"
           "background:#fff}\n"
        << ".change-card.significant{border-left:4px solid var(--sig)}\n"
        << ".change-card.critical{border-left-color:var(--crit)}\n"
        << ".change-card.cosmetic{border-left:4px solid var(--cos);opacity:.92}\n"
        << ".change-head{display:flex;flex-wrap:wrap;gap:.5rem;align-items:center;"
           "margin-bottom:.35rem;font-size:.85rem}\n"
        << ".badge{padding:.15rem .45rem;border-radius:999px;color:#fff;text-transform:uppercase;"
           "font-size:.7rem;letter-spacing:.04em}\n"
        << ".badge.significant{background:var(--sig)}.badge.critical{background:var(--crit)}"
           ".badge.cosmetic{background:var(--cos)}\n"
        << ".type{font-family:ui-monospace,Consolas,monospace;color:var(--muted)}\n"
        << ".sheet{margin-left:auto;font-family:ui-monospace,Consolas,monospace}\n"
        << ".message{margin:.25rem 0 .5rem}\n"
        << ".net-graph{display:block;width:100%;max-width:420px;height:auto;"
           "border-radius:4px}\n"
        << ".label-list{display:flex;flex-wrap:wrap;gap:.35rem;list-style:none;"
           "padding:0;margin:.5rem 0 0}\n"
        << ".label-list li{padding:.2rem .5rem;border:1px solid var(--line);border-radius:4px;"
           "background:#eef6f1;font-family:ui-monospace,Consolas,monospace;font-size:.8rem}\n"
        << ".empty{color:var(--muted);font-style:italic}\n"
        << "</style>\n</head>\n<body>\n";

    const char* gate =
        result.summary.gate == GateResult::kPass ? "PASS" : "FAIL";
    out << "<header>\n"
        << "  <h1>NetDiff visual review</h1>\n"
        << "  <p class=\"meta\">" << HtmlEscape(result.before_ref) << " → "
        << HtmlEscape(result.after_ref) << "</p>\n"
        << "  <p><span class=\"gate " << gate << "\">Gate " << gate << "</span> "
        << result.summary.significant_count << " significant, "
        << result.summary.cosmetic_count << " cosmetic</p>\n"
        << "</header>\n";

    // Group visible changes: significance → sheet → cards.
    using SheetMap = std::map<std::string, std::vector<const Change*>>;
    SheetMap significant_sheets;
    SheetMap cosmetic_sheets;
    for (const auto& change : result.changes) {
        if (!HtmlVisible(change, options)) {
            continue;
        }
        const std::string sheet = SheetOf(change);
        if (change.significance == Significance::kSignificant) {
            significant_sheets[sheet].push_back(&change);
        } else {
            cosmetic_sheets[sheet].push_back(&change);
        }
    }

    auto emit_section = [&](const char* title, const SheetMap& sheets, const char* cls) {
        out << "<section class=\"significance-section " << cls << "\" "
               "data-significance=\""
            << (std::string(cls) == "significant" ? "SIGNIFICANT" : "COSMETIC") << "\">\n";
        out << "  <h2>" << title << "</h2>\n";
        if (sheets.empty()) {
            out << "  <p class=\"empty\">No " << cls << " changes.</p>\n";
        } else {
            int index = 0;
            for (const auto& [sheet, items] : sheets) {
                out << "  <div class=\"sheet-block\" data-sheet=\"" << AttrEscape(sheet)
                    << "\">\n";
                out << "    <h3 class=\"sheet-title\">sheet: " << HtmlEscape(sheet) << " ("
                    << items.size() << ")</h3>\n";
                for (const Change* change : items) {
                    EmitChangeCard(out, *change, index++);
                }
                out << "  </div>\n";
            }
        }
        out << "</section>\n";
    };

    emit_section("Significant", significant_sheets, "significant");
    if (options.include_cosmetic || !cosmetic_sheets.empty()) {
        emit_section("Cosmetic", cosmetic_sheets, "cosmetic");
    }

    out << "<footer class=\"meta\">Generated offline by NetDiff — no external resources."
           "</footer>\n";
    out << "</body>\n</html>\n";
    return out.str();
}

}  // namespace cli
}  // namespace netdiff
