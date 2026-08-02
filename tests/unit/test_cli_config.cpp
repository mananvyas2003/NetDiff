// T1.7: .netdiff.yml parsing (02 §4, 04 §4) and pipeline step 1 normalization
// (03 §2), including the ignore globs.

#include "config.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

using netdiff::Component;
using netdiff::ConnectivityGraph;
using netdiff::Diff;
using netdiff::DiffConfig;
using netdiff::DiffResult;
using netdiff::GlobMatch;
using netdiff::Net;
using netdiff::Pin;
using netdiff::cli::ConfigLoadResult;
using netdiff::cli::ParseConfig;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << "\n";
        ++g_failures;
    }
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

ConnectivityGraph MakeGraph(std::vector<Net> nets, std::vector<std::string> refs) {
    ConnectivityGraph g;
    g.source.revision = "rev";
    g.nets = std::move(nets);
    for (const auto& ref : refs) {
        Component c;
        c.ref = ref;
        c.lib_id = "Lib:Part";
        Pin p;
        p.component_ref = ref;
        p.number = "1";
        c.pins.push_back(p);
        g.components.push_back(c);
    }
    std::sort(g.components.begin(), g.components.end(),
              [](const Component& a, const Component& b) { return a.ref < b.ref; });
    return g;
}

int CountOf(const DiffResult& result, const std::string& type) {
    const auto it = result.summary.by_type.find(type);
    return it == result.summary.by_type.end() ? 0 : it->second;
}

// The example configuration from 02_DATA_MODEL.md §4, verbatim.
void TestSpecExampleConfig() {
    const std::string text =
        "schema_version: \"1.0\"\n"
        "gate:\n"
        "  fail_on: significant          # significant | any | never\n"
        "  ignore_change_types: []       # e.g. [ComponentModified]\n"
        "net_normalization:\n"
        "  case_insensitive: false\n"
        "  aliases:                      # treat these names as equal\n"
        "    - [VCC, +5V]\n"
        "ignore:\n"
        "  components: []                # refdes globs, e.g. [\"TP*\"]\n"
        "  nets: []\n"
        "unnamed_net_matching:\n"
        "  jaccard_threshold: 0.6\n";
    const ConfigLoadResult loaded = ParseConfig(text, "<test>");

    Check(loaded.ok, "spec example parses");
    Check(loaded.config.gate.fail_on == DiffConfig::Gate::FailOn::kSignificant,
          "spec example: fail_on significant");
    Check(loaded.config.gate.ignore_change_types.empty(),
          "spec example: empty ignore_change_types");
    Check(!loaded.config.net_normalization.case_insensitive,
          "spec example: case_insensitive false");
    Check(loaded.config.net_normalization.aliases.size() == 1 &&
              loaded.config.net_normalization.aliases[0] ==
                  std::vector<std::string>{"VCC", "+5V"},
          "spec example: alias pair [VCC, +5V]");
    Check(loaded.config.ignore.components.empty() && loaded.config.ignore.nets.empty(),
          "spec example: empty ignore lists");
    Check(loaded.config.unnamed_net_matching.jaccard_threshold == 0.6,
          "spec example: jaccard 0.6");
    Check(loaded.warnings.empty(),
          "spec example: no warnings (got " + std::to_string(loaded.warnings.size()) + ")");
}

void TestNonDefaultValues() {
    const std::string text =
        "gate:\n"
        "  fail_on: any\n"
        "  ignore_change_types: [ComponentModified, NetRenamed]\n"
        "ignore:\n"
        "  components:\n"
        "    - TP*\n"
        "    - \"FID?\"\n"
        "  nets: [\"unconnected-*\"]\n"
        "unnamed_net_matching:\n"
        "  jaccard_threshold: 0.85\n";
    const ConfigLoadResult loaded = ParseConfig(text, "<test>");
    Check(loaded.ok, "non-default config parses");
    Check(loaded.config.gate.fail_on == DiffConfig::Gate::FailOn::kAny, "fail_on any");
    Check(loaded.config.gate.ignore_change_types ==
              std::vector<std::string>{"ComponentModified", "NetRenamed"},
          "inline ignore_change_types list");
    Check(loaded.config.ignore.components == std::vector<std::string>{"TP*", "FID?"},
          "block sequence with quotes");
    Check(loaded.config.ignore.nets == std::vector<std::string>{"unconnected-*"},
          "inline quoted list");
    Check(loaded.config.unnamed_net_matching.jaccard_threshold == 0.85, "jaccard override");
}

void TestBadConfigIsRejected() {
    Check(!ParseConfig("gate:\n  fail_on: sometimes\n", "<test>").ok,
          "rejects an unknown fail_on");
    Check(!ParseConfig("unnamed_net_matching:\n  jaccard_threshold: 5\n", "<test>").ok,
          "rejects an out-of-range threshold");
    Check(!ParseConfig("schema_version: \"2.0\"\n", "<test>").ok,
          "rejects an incompatible schema_version");
    Check(ParseConfig("schema_version: \"1.4\"\n", "<test>").ok,
          "accepts a compatible 1.x schema_version");

    const ConfigLoadResult unknown = ParseConfig("gate:\n  fail_onn: any\n", "<test>");
    Check(unknown.ok, "a typo does not abort the run");
    Check(!unknown.warnings.empty(), "a typo is warned about");
}

void TestCommentsAndBlankLines() {
    const std::string text =
        "# leading comment\n"
        "\n"
        "gate:\n"
        "  # nested comment\n"
        "  fail_on: never\n"
        "\n";
    const ConfigLoadResult loaded = ParseConfig(text, "<test>");
    Check(loaded.ok && loaded.config.gate.fail_on == DiffConfig::Gate::FailOn::kNever,
          "comments and blank lines are skipped");
}

void TestGlobMatch() {
    Check(GlobMatch("TP*", "TP1") && GlobMatch("TP*", "TP"), "glob: prefix star");
    Check(!GlobMatch("TP*", "R1"), "glob: non-match");
    Check(GlobMatch("FID?", "FID1") && !GlobMatch("FID?", "FID12"), "glob: single char");
    Check(GlobMatch("*", "anything"), "glob: bare star");
    Check(GlobMatch("*-*", "unconnected-U1"), "glob: star in the middle");
    Check(!GlobMatch("R1", "R10"), "glob: literal is exact");
}

// Ignored components must not produce component changes...
void TestIgnoreComponents() {
    const auto a = MakeGraph({MakeNet("N", true, {"U1.1"})}, {"U1", "TP1"});
    const auto b = MakeGraph({MakeNet("N", true, {"U1.1"})}, {"U1"});

    DiffConfig plain;
    Check(CountOf(Diff(a, b, plain), "ComponentRemoved") == 1,
          "control: the removed test point is reported");

    DiffConfig ignoring;
    ignoring.ignore.components = {"TP*"};
    const auto result = Diff(a, b, ignoring);
    Check(result.changes.empty(), "ignore.components suppresses the test point entirely");
}

// ...and ignored nets must drop out symmetrically, without inventing pin moves.
void TestIgnoreNets() {
    const auto a = MakeGraph({MakeNet("KEEP", true, {"U1.1", "U2.1"}),
                              MakeNet("DEBUG_TX", true, {"U3.1", "U4.1"})},
                             {"U1", "U2", "U3", "U4"});
    const auto b = MakeGraph({MakeNet("KEEP", true, {"U1.1", "U2.1"}),
                              MakeNet("DEBUG_TX", true, {"U3.1", "U5.1"})},
                             {"U1", "U2", "U3", "U4", "U5"});

    DiffConfig plain;
    Check(Diff(a, b, plain).summary.significant_count > 0,
          "control: the debug net change is reported");

    DiffConfig ignoring;
    ignoring.ignore.nets = {"DEBUG_*"};
    ignoring.ignore.components = {"U4", "U5"};
    const auto result = Diff(a, b, ignoring);
    Check(result.changes.empty(),
          "ignore.nets suppresses the change (got " +
              std::to_string(result.changes.size()) + ")");
}

void TestAliases() {
    const auto a = MakeGraph({MakeNet("VCC", true, {"U1.1", "U2.1"})}, {"U1", "U2"});
    const auto b = MakeGraph({MakeNet("+5V", true, {"U1.1", "U2.1"})}, {"U1", "U2"});

    DiffConfig plain;
    Check(CountOf(Diff(a, b, plain), "NetRenamed") == 1,
          "control: VCC -> +5V is a rename without aliasing");

    DiffConfig aliased;
    aliased.net_normalization.aliases = {{"VCC", "+5V"}};
    Check(Diff(a, b, aliased).changes.empty(),
          "aliases collapse VCC and +5V to one canonical name");
}

void TestCaseInsensitive() {
    const auto a = MakeGraph({MakeNet("Sda", true, {"U1.1", "U2.1"})}, {"U1", "U2"});
    const auto b = MakeGraph({MakeNet("SDA", true, {"U1.1", "U2.1"})}, {"U1", "U2"});

    DiffConfig plain;
    Check(CountOf(Diff(a, b, plain), "NetRenamed") == 1,
          "control: case difference is a rename by default");

    DiffConfig insensitive;
    insensitive.net_normalization.case_insensitive = true;
    Check(Diff(a, b, insensitive).changes.empty(),
          "case_insensitive folds Sda and SDA together");
}

// 03 §2 is explicit: normalization may rewrite names, never pin-sets.
void TestNormalizationKeepsPinSets() {
    const auto a = MakeGraph({MakeNet("VCC", true, {"U1.1", "U2.1"}),
                              MakeNet("SIG", true, {"U3.1"})},
                             {"U1", "U2", "U3", "TP9"});
    DiffConfig config;
    config.net_normalization.aliases = {{"VCC", "+5V"}};
    config.net_normalization.case_insensitive = true;
    config.ignore.components = {"TP*"};
    // Diffing a graph against itself under aggressive normalization must still
    // be silent: nothing here changes what is connected to what.
    Check(Diff(a, a, config).changes.empty(),
          "normalization is identity-preserving on an unchanged design");
}

}  // namespace

int main() {
    TestSpecExampleConfig();
    TestNonDefaultValues();
    TestBadConfigIsRejected();
    TestCommentsAndBlankLines();
    TestGlobMatch();
    TestIgnoreComponents();
    TestIgnoreNets();
    TestAliases();
    TestCaseInsensitive();
    TestNormalizationKeepsPinSets();

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "cli config + normalization: ok\n";
    return 0;
}
