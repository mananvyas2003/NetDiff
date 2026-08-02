#pragma once

// Diff data model — 02_DATA_MODEL.md §3.
//
// A Change carries its payload in a std::variant whose alternatives are in the
// same order as ChangeType, so the discriminator is derived (Change::type())
// rather than stored: the tag and the payload can never disagree.

#include <map>
#include <string>
#include <variant>
#include <vector>

#include "netdiff/graph.hpp"

namespace netdiff {

// §3.1. Significance is two-valued by spec; criticality (a power-net merge, a
// refdes reused with a different lib_id) rides alongside as Change::critical
// because 03_DIFF_ALGORITHM.md §6/§7 escalate severity without introducing a
// third significance level, and SARIF needs error/warning/note (04 §1.4).
enum class Significance { kCosmetic, kSignificant };

enum class GateResult { kPass, kFail };

// §3.2, in the order the spec lists them. ChangePayload mirrors this order.
enum class ChangeType {
    kComponentAdded,
    kComponentRemoved,
    kComponentModified,
    kNetAdded,
    kNetRemoved,
    kNetRenamed,
    kNetMerged,
    kNetSplit,
    kPinConnectionChanged,
};

// Net name reported for a pin that is on no net (03_DIFF_ALGORITHM.md §5).
extern const char* const kUnconnectedNetName;

const char* ToString(Significance significance);
const char* ToString(GateResult gate);
const char* ToString(ChangeType type);

struct FieldChange {
    std::string field;
    std::string before;
    std::string after;
};

struct ComponentAdded {
    Component component;
};

struct ComponentRemoved {
    Component component;
};

struct ComponentModified {
    std::string ref;
    std::vector<FieldChange> changes;
};

struct NetAdded {
    Net net;
};

struct NetRemoved {
    Net net;
};

struct NetRenamed {
    std::string before_name;
    std::string after_name;
    Net net;
};

struct NetMerged {
    std::vector<std::string> before_nets;
    std::string after_net;
    std::vector<std::string> pins_involved;
};

struct NetSplit {
    std::string before_net;
    std::vector<std::string> after_nets;
    std::vector<std::string> pins_involved;
};

struct PinConnectionChanged {
    std::string pin;
    std::string before_net;
    std::string after_net;
};

using ChangePayload = std::variant<ComponentAdded,
                                   ComponentRemoved,
                                   ComponentModified,
                                   NetAdded,
                                   NetRemoved,
                                   NetRenamed,
                                   NetMerged,
                                   NetSplit,
                                   PinConnectionChanged>;

struct Change {
    ChangePayload payload;
    Significance significance = Significance::kSignificant;
    bool critical = false;
    std::string message;

    ChangeType type() const;
};

// Stable per-change key used to order equal (significance, type) changes:
// the PinId, net name or ComponentId the change is about (03 §9).
std::string ChangeSortKey(const Change& change);

// 03_DIFF_ALGORITHM.md §9: significance descending, then type, then sort key.
void SortChanges(std::vector<Change>& changes);

struct DiffSummary {
    int significant_count = 0;
    int cosmetic_count = 0;
    // Type name → count; only non-zero types appear. std::map keeps emission
    // ordered without a sort at serialization time.
    std::map<std::string, int> by_type;
    GateResult gate = GateResult::kPass;
};

struct DiffResult {
    std::string schema_version = "1.0";
    std::string before_ref;
    std::string after_ref;
    DiffSummary summary;
    std::vector<Change> changes;
};

// Recompute significant_count / cosmetic_count / by_type from `changes`.
// The gate verdict is left untouched — it is config-driven (T1.5, 03 §8).
void RecomputeCounts(DiffResult& result);

// Knobs from 02_DATA_MODEL.md §4. Fields appear as the pipeline stages that
// consume them land.
struct DiffConfig {
    struct Gate {
        enum class FailOn { kSignificant, kAny, kNever };
        FailOn fail_on = FailOn::kSignificant;
        // Change type names (as ToString(ChangeType) spells them) that must not
        // influence the gate. Such changes are still reported — 02 §4 describes
        // the list as "to not gate on", not "to hide".
        std::vector<std::string> ignore_change_types;
    } gate;

    struct UnnamedNetMatching {
        // Minimum pin-set Jaccard similarity for tier 3 to call two nets the
        // same (02 §4 calls it a minimum, so the comparison is inclusive).
        double jaccard_threshold = 0.6;
    } unnamed_net_matching;

    // Step 1 of the pipeline (03 §2). Applied to both graphs before matching;
    // it only ever rewrites names and drops ignored entries — pin-sets are
    // never touched, because they are the nets' identity.
    struct NetNormalization {
        bool case_insensitive = false;
        // Each group is a set of names to treat as equal; the first element is
        // the canonical form the others are rewritten to.
        std::vector<std::vector<std::string>> aliases;
    } net_normalization;

    struct Ignore {
        // Glob patterns ('*' and '?'), e.g. "TP*" for test points.
        std::vector<std::string> components;
        std::vector<std::string> nets;
    } ignore;
};

// Glob match supporting '*' and '?', used by DiffConfig::Ignore. Exposed for
// testing; matching is case-sensitive.
bool GlobMatch(const std::string& pattern, const std::string& text);

// The semantic diff (03_DIFF_ALGORITHM.md). Pure and deterministic: identical
// inputs always yield an identical DiffResult.
//
// Implemented so far: step 2, component matching (03 §3); step 3, net matching
// tiers 1-3 (03 §4).
DiffResult Diff(const ConnectivityGraph& before, const ConnectivityGraph& after,
                const DiffConfig& config);

// Deterministic JSON: sorted keys, identical input → byte-identical output.
std::string SerializeDiffJson(const DiffResult& result);

}  // namespace netdiff
