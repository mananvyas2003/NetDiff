// T1.6: output formatters — 04_CLI_CI_SPEC.md §1.4, 06_TESTING_QA.md §6.
//
// argv[1] = tests/golden directory. Mismatches are written next to the golden
// as *.actual so the difference is inspectable.

#include "format.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using netdiff::Change;
using netdiff::ChangeType;
using netdiff::Component;
using netdiff::ComponentModified;
using netdiff::DiffResult;
using netdiff::FieldChange;
using netdiff::GateResult;
using netdiff::Net;
using netdiff::NetAdded;
using netdiff::NetMerged;
using netdiff::NetRenamed;
using netdiff::Pin;
using netdiff::PinConnectionChanged;
using netdiff::Significance;
using netdiff::cli::FormatDiff;
using netdiff::cli::FormatOptions;
using netdiff::cli::OutputFormat;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << "\n";
        ++g_failures;
    }
}

std::string ReadFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void CheckGolden(const std::string& path, const std::string& actual) {
    if (ReadFile(path) == actual) {
        return;
    }
    std::ofstream out(path + ".actual", std::ios::binary);
    out << actual;
    Check(false, "golden mismatch: " + path + " (wrote " + path + ".actual)");
}

Net MakeNet(const std::string& name, bool is_named, std::vector<std::string> pins) {
    Net n;
    n.name = name;
    n.is_named = is_named;
    std::sort(pins.begin(), pins.end());
    n.pins = std::move(pins);
    for (const auto& p : n.pins) {
        n.net_id += p;
        n.net_id += "|";
    }
    return n;
}

// One change per section the formatters can produce: a critical merge, a pin
// move, a net add, a component edit, and a cosmetic rename.
DiffResult MakeSample() {
    DiffResult result;
    result.before_ref = "HEAD~1";
    result.after_ref = "working-tree";

    {
        Change c;
        c.payload = NetMerged{{"+3V3", "+5V"}, "+5V", {"C12.1", "U3.14"}};
        c.critical = true;
        c.message = "Nets +3V3 and +5V merged into +5V (via C12.1, U3.14)";
        result.changes.push_back(std::move(c));
    }
    {
        Change c;
        c.payload = PinConnectionChanged{"U3.7", "GND", "+3V3"};
        c.message = "U3.7 moved from GND to +3V3";
        result.changes.push_back(std::move(c));
    }
    {
        Change c;
        c.payload = NetAdded{MakeNet("I2C_SCL", true, {"U3.8", "R6.2"})};
        c.message = "Net I2C_SCL added";
        result.changes.push_back(std::move(c));
    }
    {
        Change c;
        c.payload = ComponentModified{"R5", {FieldChange{"value", "10k", "4k7"}}};
        c.message = "R5: value 10k -> 4k7";
        result.changes.push_back(std::move(c));
    }
    {
        Change c;
        c.payload = NetRenamed{"Net-(R4-Pad2)", "Net-(R4-Pad3)",
                               MakeNet("Net-(R4-Pad3)", false, {"R4.2", "R5.1"})};
        c.significance = Significance::kCosmetic;
        c.message = "Net renamed: Net-(R4-Pad2) -> Net-(R4-Pad3)";
        result.changes.push_back(std::move(c));
    }

    netdiff::SortChanges(result.changes);
    netdiff::RecomputeCounts(result);
    result.summary.gate = GateResult::kFail;
    return result;
}

DiffResult MakeClean() {
    DiffResult result;
    result.before_ref = "HEAD~1";
    result.after_ref = "working-tree";
    netdiff::RecomputeCounts(result);
    result.summary.gate = GateResult::kPass;
    return result;
}

FormatOptions BaseOptions() {
    FormatOptions options;
    options.color = false;
    options.include_cosmetic = false;
    options.tool_version = "0.1.0";
    options.artifact_uri = "tests/corpus/pic_programmer/pic_programmer.kicad_sch";
    return options;
}

void TestRuleIds() {
    Check(netdiff::cli::RuleIdFor(ChangeType::kPinConnectionChanged) ==
              "pin-connection-changed",
          "ruleId: PinConnectionChanged -> pin-connection-changed");
    Check(netdiff::cli::RuleIdFor(ChangeType::kNetMerged) == "net-merged",
          "ruleId: NetMerged -> net-merged");
    Check(netdiff::cli::RuleIdFor(ChangeType::kComponentAdded) == "component-added",
          "ruleId: ComponentAdded -> component-added");
}

void TestFormatParsing() {
    OutputFormat format = OutputFormat::kJson;
    Check(netdiff::cli::ParseOutputFormat("text", &format) && format == OutputFormat::kText,
          "parse: text");
    Check(netdiff::cli::ParseOutputFormat("markdown", &format) &&
              format == OutputFormat::kMarkdown,
          "parse: markdown");
    Check(netdiff::cli::ParseOutputFormat("sarif", &format) && format == OutputFormat::kSarif,
          "parse: sarif");
    Check(!netdiff::cli::ParseOutputFormat("yaml", &format), "parse: rejects unknown format");
}

void TestCosmeticHiddenByDefault() {
    const DiffResult sample = MakeSample();
    const std::string plain = FormatDiff(sample, OutputFormat::kText, BaseOptions());
    Check(plain.find("Net renamed") == std::string::npos,
          "text: cosmetic change hidden by default");

    FormatOptions with_cosmetic = BaseOptions();
    with_cosmetic.include_cosmetic = true;
    const std::string shown = FormatDiff(sample, OutputFormat::kText, with_cosmetic);
    Check(shown.find("Net renamed") != std::string::npos,
          "text: --include-cosmetic reveals it");
    Check(shown.find("Cosmetic") != std::string::npos, "text: cosmetic section header");

    // JSON is for tooling and always carries the whole DiffResult.
    const std::string json = FormatDiff(sample, OutputFormat::kJson, BaseOptions());
    Check(json.find("NetRenamed") != std::string::npos,
          "json: cosmetic change always present");
}

void TestColorOnlyWhenAsked() {
    const DiffResult sample = MakeSample();
    Check(FormatDiff(sample, OutputFormat::kText, BaseOptions()).find("\033[") ==
              std::string::npos,
          "text: no ANSI escapes without --color");
    FormatOptions colored = BaseOptions();
    colored.color = true;
    Check(FormatDiff(sample, OutputFormat::kText, colored).find("\033[") != std::string::npos,
          "text: ANSI escapes when colour is on");
}

void TestCleanDiffMessaging() {
    const std::string text = FormatDiff(MakeClean(), OutputFormat::kText, BaseOptions());
    Check(text.find("No electrical changes.") != std::string::npos,
          "text: clean diff says so plainly");
    const std::string markdown =
        FormatDiff(MakeClean(), OutputFormat::kMarkdown, BaseOptions());
    Check(markdown.find("No electrical changes.") != std::string::npos,
          "markdown: clean diff says so plainly");
    Check(markdown.find("**PASS**") != std::string::npos, "markdown: clean diff is PASS");
}

void TestSarifLevels() {
    FormatOptions options = BaseOptions();
    options.include_cosmetic = true;
    const std::string sarif = FormatDiff(MakeSample(), OutputFormat::kSarif, options);
    Check(sarif.find("\"level\": \"error\"") != std::string::npos,
          "sarif: critical change maps to error");
    Check(sarif.find("\"level\": \"warning\"") != std::string::npos,
          "sarif: significant change maps to warning");
    Check(sarif.find("\"level\": \"note\"") != std::string::npos,
          "sarif: cosmetic change maps to note");
    Check(sarif.find("pic_programmer.kicad_sch") != std::string::npos,
          "sarif: result carries the schematic location");
}

void TestDeterminism() {
    const DiffResult sample = MakeSample();
    for (OutputFormat format : {OutputFormat::kText, OutputFormat::kJson,
                                OutputFormat::kMarkdown, OutputFormat::kSarif}) {
        const std::string first = FormatDiff(sample, format, BaseOptions());
        for (int i = 0; i < 20; ++i) {
            if (FormatDiff(sample, format, BaseOptions()) != first) {
                Check(false, "formatter output is stable across runs");
                return;
            }
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: test_format <golden-dir>\n";
        return 2;
    }
    const std::string golden_dir = argv[1];

    TestRuleIds();
    TestFormatParsing();
    TestCosmeticHiddenByDefault();
    TestColorOnlyWhenAsked();
    TestCleanDiffMessaging();
    TestSarifLevels();
    TestDeterminism();

    const DiffResult sample = MakeSample();
    FormatOptions options = BaseOptions();
    options.include_cosmetic = true;

    CheckGolden(golden_dir + "/format_sample.txt",
                FormatDiff(sample, OutputFormat::kText, options));
    CheckGolden(golden_dir + "/format_sample.md",
                FormatDiff(sample, OutputFormat::kMarkdown, options));
    CheckGolden(golden_dir + "/format_sample.sarif.json",
                FormatDiff(sample, OutputFormat::kSarif, options));

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "formatters: ok\n";
    return 0;
}
