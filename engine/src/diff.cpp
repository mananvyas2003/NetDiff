// The semantic diff — 03_DIFF_ALGORITHM.md, steps 1-8.

#include "netdiff/diff.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace netdiff {
namespace {

// ---------------------------------------------------------------------------
// Step 1 — normalization (03 §2)
// ---------------------------------------------------------------------------

std::string ToLower(const std::string& text) {
    std::string out = text;
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

bool MatchesAny(const std::vector<std::string>& patterns, const std::string& text) {
    for (const auto& pattern : patterns) {
        if (GlobMatch(pattern, text)) {
            return true;
        }
    }
    return false;
}

// Rewrites names and drops ignored entries. Pin-sets are left exactly as they
// were: 03 §2 forbids touching them, and net_id stays valid as a result.
ConnectivityGraph Normalize(const ConnectivityGraph& graph, const DiffConfig& config) {
    std::map<std::string, std::string> canonical;
    for (const auto& group : config.net_normalization.aliases) {
        if (group.size() < 2) {
            continue;
        }
        for (size_t i = 1; i < group.size(); ++i) {
            canonical.emplace(group[i], group[0]);
        }
    }

    ConnectivityGraph out = graph;

    if (!config.ignore.components.empty()) {
        std::vector<Component> kept;
        kept.reserve(out.components.size());
        for (auto& component : out.components) {
            if (!MatchesAny(config.ignore.components, component.ref)) {
                kept.push_back(std::move(component));
            }
        }
        out.components = std::move(kept);
    }

    std::vector<Net> kept_nets;
    kept_nets.reserve(out.nets.size());
    for (auto& net : out.nets) {
        if (!config.ignore.nets.empty() && MatchesAny(config.ignore.nets, net.name)) {
            continue;
        }
        const auto alias = canonical.find(net.name);
        if (alias != canonical.end()) {
            net.name = alias->second;
        }
        if (config.net_normalization.case_insensitive) {
            net.name = ToLower(net.name);
        }
        kept_nets.push_back(std::move(net));
    }
    out.nets = std::move(kept_nets);

    out.stats.component_count = static_cast<int>(out.components.size());
    out.stats.net_count = static_cast<int>(out.nets.size());
    return out;
}

bool NeedsNormalization(const DiffConfig& config) {
    return config.net_normalization.case_insensitive ||
           !config.net_normalization.aliases.empty() ||
           !config.ignore.components.empty() || !config.ignore.nets.empty();
}

std::string Describe(const Component& component) {
    // "R17 (10k)" — the value is what a reviewer recognises the part by, but
    // plenty of parts (connectors, test points) carry none.
    if (component.value.empty()) {
        return component.ref;
    }
    return component.ref + " (" + component.value + ")";
}

Change MakeComponentAdded(const Component& component) {
    Change change;
    change.payload = ComponentAdded{component};
    change.significance = Significance::kSignificant;
    change.message = Describe(component) + " added";
    return change;
}

Change MakeComponentRemoved(const Component& component) {
    Change change;
    change.payload = ComponentRemoved{component};
    change.significance = Significance::kSignificant;
    change.message = Describe(component) + " removed";
    return change;
}

// 03 §3 refdes-reuse guard: one ComponentId whose lib_id changed is a different
// part at the same designator (a resistor became a capacitor), not an edit of
// the same part. Report Removed + Added instead of Modified, and mark both
// critical — it is usually a mistake.
void EmitRefdesReuse(const Component& before, const Component& after,
                     std::vector<Change>& out) {
    const std::string reason = " (refdes reused with a different symbol: " +
                               before.lib_id + " -> " + after.lib_id + ")";
    Change removed = MakeComponentRemoved(before);
    removed.critical = true;
    removed.message += reason;
    Change added = MakeComponentAdded(after);
    added.critical = true;
    added.message += reason;
    out.push_back(std::move(removed));
    out.push_back(std::move(added));
}

// Field order follows the spec's enumeration {value, footprint, lib_id} rather
// than being sorted, so messages read the way the spec lists them. lib_id never
// reaches here: a changed lib_id is handled by the refdes-reuse guard above.
std::vector<FieldChange> FieldChanges(const Component& before, const Component& after) {
    std::vector<FieldChange> changes;
    if (before.value != after.value) {
        changes.push_back(FieldChange{"value", before.value, after.value});
    }
    if (before.footprint != after.footprint) {
        changes.push_back(FieldChange{"footprint", before.footprint, after.footprint});
    }
    return changes;
}

Change MakeComponentModified(const std::string& ref, std::vector<FieldChange> changes) {
    std::string detail;
    for (size_t i = 0; i < changes.size(); ++i) {
        if (i != 0) {
            detail += ", ";
        }
        detail += changes[i].field + " " + changes[i].before + " -> " + changes[i].after;
    }
    Change change;
    change.message = ref + ": " + detail;
    change.payload = ComponentModified{ref, std::move(changes)};
    change.significance = Significance::kSignificant;
    return change;
}

// std::map, not unordered_map: iteration feeds change emission, and 03 §4
// forbids depending on hash-map order. ComponentId is unique per graph
// (02 §2.2), so a later duplicate would be a build_graph defect; keeping the
// first occurrence is deterministic given components are sorted by id.
std::map<std::string, const Component*> IndexById(const ConnectivityGraph& graph) {
    std::map<std::string, const Component*> index;
    for (const auto& component : graph.components) {
        index.emplace(MakeComponentId(component), &component);
    }
    return index;
}

// Components the diff considers to be the same part in both revisions. A
// refdes-reuse pair is deliberately absent: it was reported as remove + add, so
// its pins are not "present in both revisions" for step 4.
using ComponentMatching = std::vector<std::pair<const Component*, const Component*>>;

ComponentMatching MatchComponents(const ConnectivityGraph& before,
                                  const ConnectivityGraph& after,
                                  std::vector<Change>& out) {
    const auto before_index = IndexById(before);
    const auto after_index = IndexById(after);
    ComponentMatching matched;

    for (const auto& entry : before_index) {
        const auto it = after_index.find(entry.first);
        if (it == after_index.end()) {
            out.push_back(MakeComponentRemoved(*entry.second));
            continue;
        }
        const Component& a = *entry.second;
        const Component& b = *it->second;
        if (a.lib_id != b.lib_id) {
            EmitRefdesReuse(a, b, out);
            continue;
        }
        matched.emplace_back(&a, &b);
        std::vector<FieldChange> field_changes = FieldChanges(a, b);
        if (!field_changes.empty()) {
            out.push_back(MakeComponentModified(b.ref, std::move(field_changes)));
        }
    }

    for (const auto& entry : after_index) {
        if (before_index.find(entry.first) == before_index.end()) {
            out.push_back(MakeComponentAdded(*entry.second));
        }
    }
    return matched;
}

// ---------------------------------------------------------------------------
// Step 3 — net matching (03 §4)
// ---------------------------------------------------------------------------

// Pairs of indices into before.nets / after.nets that the tiers decided are the
// same net. Step 4 (pin-level changes) reads `matched`; `removed` / `added` are
// the nets no tier could pair.
struct NetMatching {
    std::vector<std::pair<size_t, size_t>> matched;
    std::vector<size_t> removed;
    std::vector<size_t> added;
};

// Jaccard kept as an exact rational: comparing intersection*other_union avoids
// deciding tie-breaks on floating-point rounding.
struct Similarity {
    int64_t intersection = 0;
    int64_t union_size = 0;
};

bool MoreSimilar(const Similarity& a, const Similarity& b) {
    return a.intersection * b.union_size > b.intersection * a.union_size;
}

bool SameSimilarity(const Similarity& a, const Similarity& b) {
    return a.intersection * b.union_size == b.intersection * a.union_size;
}

bool MeetsThreshold(const Similarity& s, double threshold) {
    if (s.union_size == 0) {
        return false;
    }
    return static_cast<double>(s.intersection) >=
           threshold * static_cast<double>(s.union_size);
}

Similarity Score(const Net& a, const Net& b) {
    const std::unordered_set<std::string> a_pins(a.pins.begin(), a.pins.end());
    int64_t shared = 0;
    for (const auto& pin : b.pins) {
        if (a_pins.count(pin) != 0) {
            ++shared;
        }
    }
    Similarity s;
    s.intersection = shared;
    s.union_size = static_cast<int64_t>(a_pins.size()) +
                   static_cast<int64_t>(b.pins.size()) - shared;
    return s;
}

struct Candidate {
    Similarity similarity;
    size_t before = 0;
    size_t after = 0;
    bool same_name = false;
};

// 03 §4: when similarities tie, break by (a) name equality, then (b)
// lexicographic NetId. net_id is unique within a graph, so this is a total
// order and the greedy pass cannot depend on hash iteration order.
bool BetterCandidate(const Candidate& a, const Candidate& b,
                     const ConnectivityGraph& before, const ConnectivityGraph& after) {
    if (!SameSimilarity(a.similarity, b.similarity)) {
        return MoreSimilar(a.similarity, b.similarity);
    }
    if (a.same_name != b.same_name) {
        return a.same_name;
    }
    const auto& ab = before.nets[a.before].net_id;
    const auto& bb = before.nets[b.before].net_id;
    if (ab != bb) {
        return ab < bb;
    }
    return after.nets[a.after].net_id < after.nets[b.after].net_id;
}

// Score the candidate pairs, best first, and take each pair whose two nets are
// both still free.
void GreedyPair(const ConnectivityGraph& before, const ConnectivityGraph& after,
                std::vector<Candidate>& candidates, std::vector<bool>& before_used,
                std::vector<bool>& after_used, NetMatching& out) {
    std::sort(candidates.begin(), candidates.end(),
              [&before, &after](const Candidate& a, const Candidate& b) {
                  return BetterCandidate(a, b, before, after);
              });
    for (const auto& candidate : candidates) {
        if (before_used[candidate.before] || after_used[candidate.after]) {
            continue;
        }
        before_used[candidate.before] = true;
        after_used[candidate.after] = true;
        out.matched.emplace_back(candidate.before, candidate.after);
    }
}

std::map<std::string, std::vector<size_t>> IndexByName(const ConnectivityGraph& graph) {
    std::map<std::string, std::vector<size_t>> index;
    for (size_t i = 0; i < graph.nets.size(); ++i) {
        // An unnamed net's name is empty (build_graph leaves it so); it carries
        // no naming evidence, so it must not join the name namespace — half the
        // nets on a big board would collide there.
        if (graph.nets[i].name.empty()) {
            continue;
        }
        index[graph.nets[i].name].push_back(i);
    }
    return index;
}

// Tier 1 — exact name match. A name is not unique (local labels repeat across
// sheets: vme-wren has four TERM_EN0), so a name group is paired by pin-set
// overlap rather than assumed to be one net.
void MatchTierName(const ConnectivityGraph& before, const ConnectivityGraph& after,
                   std::vector<bool>& before_used, std::vector<bool>& after_used,
                   NetMatching& out) {
    const auto before_by_name = IndexByName(before);
    const auto after_by_name = IndexByName(after);
    for (const auto& entry : before_by_name) {
        const auto it = after_by_name.find(entry.first);
        if (it == after_by_name.end()) {
            continue;
        }
        // Equal names are candidate matches whatever their pin-sets, so the
        // whole cross product is offered — no threshold applies at this tier.
        // Groups are tiny, so the quadratic pairing is free.
        std::vector<Candidate> candidates;
        for (size_t bi : entry.second) {
            for (size_t ai : it->second) {
                Candidate c;
                c.before = bi;
                c.after = ai;
                c.same_name = true;
                c.similarity = Score(before.nets[bi], after.nets[ai]);
                candidates.push_back(c);
            }
        }
        GreedyPair(before, after, candidates, before_used, after_used, out);
    }
}

// Tier 2 — identical pin-set. net_id is the hash of the sorted pin-set and is
// unique within a graph, so this is a plain join. Equal pin-set with a
// different name is a rename; with the same name it is simply unchanged.
void MatchTierPinSet(const ConnectivityGraph& before, const ConnectivityGraph& after,
                     std::vector<bool>& before_used, std::vector<bool>& after_used,
                     NetMatching& out) {
    std::map<std::string, size_t> after_by_id;
    for (size_t i = 0; i < after.nets.size(); ++i) {
        if (!after_used[i]) {
            after_by_id.emplace(after.nets[i].net_id, i);
        }
    }
    for (size_t i = 0; i < before.nets.size(); ++i) {
        if (before_used[i]) {
            continue;
        }
        const auto it = after_by_id.find(before.nets[i].net_id);
        if (it == after_by_id.end() || after_used[it->second]) {
            continue;
        }
        before_used[i] = true;
        after_used[it->second] = true;
        out.matched.emplace_back(i, it->second);
    }
}

// Tier 3 — fuzzy pin-set match for what is left. Candidates come from a
// pin->net index so the pass stays linear in shared pins instead of quadratic
// in nets (03 §10); nets sharing no pin score 0 and could never pass anyway.
void MatchTierFuzzy(const ConnectivityGraph& before, const ConnectivityGraph& after,
                    double threshold, std::vector<bool>& before_used,
                    std::vector<bool>& after_used, NetMatching& out) {
    std::unordered_map<std::string, std::vector<size_t>> after_by_pin;
    for (size_t i = 0; i < after.nets.size(); ++i) {
        if (after_used[i]) {
            continue;
        }
        for (const auto& pin : after.nets[i].pins) {
            after_by_pin[pin].push_back(i);
        }
    }

    std::vector<Candidate> candidates;
    for (size_t i = 0; i < before.nets.size(); ++i) {
        if (before_used[i]) {
            continue;
        }
        std::vector<size_t> seen;
        for (const auto& pin : before.nets[i].pins) {
            const auto it = after_by_pin.find(pin);
            if (it == after_by_pin.end()) {
                continue;
            }
            seen.insert(seen.end(), it->second.begin(), it->second.end());
        }
        std::sort(seen.begin(), seen.end());
        seen.erase(std::unique(seen.begin(), seen.end()), seen.end());
        for (size_t ai : seen) {
            Candidate c;
            c.before = i;
            c.after = ai;
            c.same_name = before.nets[i].name == after.nets[ai].name;
            c.similarity = Score(before.nets[i], after.nets[ai]);
            if (!MeetsThreshold(c.similarity, threshold)) {
                continue;
            }
            candidates.push_back(c);
        }
    }
    GreedyPair(before, after, candidates, before_used, after_used, out);
}

std::string DescribeNet(const Net& net) {
    if (!net.name.empty()) {
        return "Net " + net.name;
    }
    // Unnamed nets have nothing to quote but their pins.
    std::string detail;
    for (size_t i = 0; i < net.pins.size() && i < 3; ++i) {
        if (i != 0) {
            detail += ", ";
        }
        detail += net.pins[i];
    }
    if (net.pins.size() > 3) {
        detail += ", ...";
    }
    return "Unnamed net (" + detail + ")";
}

std::string NetNameForMessage(const Net& net) {
    return net.name.empty() ? "(unnamed)" : net.name;
}

NetMatching MatchNets(const ConnectivityGraph& before, const ConnectivityGraph& after,
                      const DiffConfig& config, std::vector<Change>& out) {
    NetMatching matching;
    std::vector<bool> before_used(before.nets.size(), false);
    std::vector<bool> after_used(after.nets.size(), false);

    MatchTierName(before, after, before_used, after_used, matching);
    MatchTierPinSet(before, after, before_used, after_used, matching);
    MatchTierFuzzy(before, after, config.unnamed_net_matching.jaccard_threshold,
                   before_used, after_used, matching);

    // A matched pair whose name changed is a rename. Pairs that differ only in
    // pin membership produce no change here — those are step 4's business.
    std::sort(matching.matched.begin(), matching.matched.end());
    for (const auto& pair : matching.matched) {
        const Net& a = before.nets[pair.first];
        const Net& b = after.nets[pair.second];
        if (a.name == b.name) {
            continue;
        }
        Change change;
        change.payload = NetRenamed{a.name, b.name, b};
        // 03 §4 tier 2: a user-facing name changing is something reviewers care
        // about; two auto-generated names differing is not.
        change.significance = (a.is_named || b.is_named) ? Significance::kSignificant
                                                         : Significance::kCosmetic;
        change.message =
            "Net renamed: " + NetNameForMessage(a) + " -> " + NetNameForMessage(b);
        out.push_back(std::move(change));
    }

    for (size_t i = 0; i < before.nets.size(); ++i) {
        if (before_used[i]) {
            continue;
        }
        matching.removed.push_back(i);
        Change change;
        change.payload = NetRemoved{before.nets[i]};
        change.significance = Significance::kSignificant;
        change.message = DescribeNet(before.nets[i]) + " removed";
        out.push_back(std::move(change));
    }
    for (size_t i = 0; i < after.nets.size(); ++i) {
        if (after_used[i]) {
            continue;
        }
        matching.added.push_back(i);
        Change change;
        change.payload = NetAdded{after.nets[i]};
        change.significance = Significance::kSignificant;
        change.message = DescribeNet(after.nets[i]) + " added";
        out.push_back(std::move(change));
    }

    return matching;
}

// ---------------------------------------------------------------------------
// Step 4 — pin-level connectivity changes (03 §5)
// ---------------------------------------------------------------------------

constexpr size_t kNoNet = static_cast<size_t>(-1);

std::unordered_map<std::string, size_t> IndexPinsByNet(const ConnectivityGraph& graph) {
    std::unordered_map<std::string, size_t> index;
    for (size_t i = 0; i < graph.nets.size(); ++i) {
        for (const auto& pin : graph.nets[i].pins) {
            index.emplace(pin, i);
        }
    }
    return index;
}

std::string NetLabel(const ConnectivityGraph& graph, size_t net_index) {
    if (net_index == kNoNet) {
        return kUnconnectedNetName;
    }
    return NetNameForMessage(graph.nets[net_index]);
}

// One pin that moved, kept so step 5 can attribute merges and splits to the
// pins that caused them.
struct MovedPin {
    std::string pin;
    size_t before_net = kNoNet;
    size_t after_net = kNoNet;
};

// 03 §5. "Did this pin's net change?" is decided on net *identity* — the
// pairing from step 3 — not on the net's name. Comparing names would make a
// plain rename look like every one of its pins was re-tied, which would break
// the flagship invariant; the names are only what the message reports.
std::vector<MovedPin> EmitPinChanges(const ConnectivityGraph& before,
                                     const ConnectivityGraph& after,
                                     const ComponentMatching& components,
                                     const NetMatching& nets, std::vector<Change>& out) {
    std::unordered_map<size_t, size_t> before_to_after;
    for (const auto& pair : nets.matched) {
        before_to_after.emplace(pair.first, pair.second);
    }
    const auto before_pins = IndexPinsByNet(before);
    const auto after_pins = IndexPinsByNet(after);

    // Only pins carried by a component that exists in both revisions; a pin
    // that came or went with its component is already reported as such.
    std::vector<std::string> pin_ids;
    for (const auto& pair : components) {
        std::unordered_set<std::string> after_numbers;
        for (const auto& pin : pair.second->pins) {
            after_numbers.insert(pin.number);
        }
        for (const auto& pin : pair.first->pins) {
            if (after_numbers.count(pin.number) != 0) {
                pin_ids.push_back(MakePinId(pin));
            }
        }
    }
    std::sort(pin_ids.begin(), pin_ids.end());
    pin_ids.erase(std::unique(pin_ids.begin(), pin_ids.end()), pin_ids.end());

    std::vector<MovedPin> moved;
    for (const auto& pin_id : pin_ids) {
        const auto before_it = before_pins.find(pin_id);
        const auto after_it = after_pins.find(pin_id);
        const size_t before_net = before_it == before_pins.end() ? kNoNet : before_it->second;
        const size_t after_net = after_it == after_pins.end() ? kNoNet : after_it->second;

        if (before_net == kNoNet && after_net == kNoNet) {
            continue;  // unconnected in both revisions
        }
        if (before_net != kNoNet && after_net != kNoNet) {
            const auto match = before_to_after.find(before_net);
            if (match != before_to_after.end() && match->second == after_net) {
                continue;  // same net, whatever it is now called
            }
        }

        MovedPin move;
        move.pin = pin_id;
        move.before_net = before_net;
        move.after_net = after_net;
        moved.push_back(move);

        Change change;
        change.payload = PinConnectionChanged{pin_id, NetLabel(before, before_net),
                                              NetLabel(after, after_net)};
        change.significance = Significance::kSignificant;  // always (03 §5)
        change.message = pin_id + " moved from " + NetLabel(before, before_net) + " to " +
                         NetLabel(after, after_net);
        out.push_back(std::move(change));
    }
    return moved;
}

// ---------------------------------------------------------------------------
// Step 5 — net merge / split detection (03 §6)
// ---------------------------------------------------------------------------

class Dsu {
public:
    explicit Dsu(size_t n) : parent_(n) {
        for (size_t i = 0; i < n; ++i) {
            parent_[i] = i;
        }
    }
    size_t Find(size_t x) {
        while (parent_[x] != x) {
            parent_[x] = parent_[parent_[x]];
            x = parent_[x];
        }
        return x;
    }
    void Union(size_t a, size_t b) {
        a = Find(a);
        b = Find(b);
        if (a != b) {
            parent_[b] = a;
        }
    }

private:
    std::vector<size_t> parent_;
};

std::string JoinNames(const std::vector<std::string>& names) {
    std::string out;
    for (size_t i = 0; i < names.size(); ++i) {
        if (i != 0) {
            out += (i + 1 == names.size()) ? " and " : ", ";
        }
        out += names[i].empty() ? "(unnamed)" : names[i];
    }
    return out;
}

std::string JoinPins(const std::vector<std::string>& pins) {
    if (pins.empty()) {
        return std::string();
    }
    std::string out = " (via ";
    for (size_t i = 0; i < pins.size() && i < 3; ++i) {
        if (i != 0) {
            out += ", ";
        }
        out += pins[i];
    }
    if (pins.size() > 3) {
        out += ", ...";
    }
    return out + ")";
}

// 03 §6: build the bipartite graph of before-nets and after-nets induced by
// shared pins, then read each connected component. Many-to-one is a merge,
// one-to-many a split, one-to-one a rename or no change at all. Many-to-many is
// a general restructuring that the spec does not classify — the per-pin changes
// from step 4 already describe it, so nothing extra is emitted.
void DetectMergesAndSplits(const ConnectivityGraph& before, const ConnectivityGraph& after,
                           const std::vector<MovedPin>& moved, std::vector<Change>& out) {
    const size_t a_count = before.nets.size();
    const size_t b_count = after.nets.size();
    if (a_count == 0 || b_count == 0) {
        return;
    }
    Dsu dsu(a_count + b_count);

    const auto after_pins = IndexPinsByNet(after);
    for (size_t i = 0; i < a_count; ++i) {
        for (const auto& pin : before.nets[i].pins) {
            const auto it = after_pins.find(pin);
            if (it != after_pins.end()) {
                dsu.Union(i, a_count + it->second);
            }
        }
    }

    std::map<size_t, std::vector<size_t>> before_groups;
    std::map<size_t, std::vector<size_t>> after_groups;
    for (size_t i = 0; i < a_count; ++i) {
        before_groups[dsu.Find(i)].push_back(i);
    }
    for (size_t j = 0; j < b_count; ++j) {
        after_groups[dsu.Find(a_count + j)].push_back(j);
    }

    // Pins that actually moved, grouped by the component they moved within —
    // this is what "(via C12.1)" in the spec's example message points at.
    std::map<size_t, std::vector<std::string>> moved_by_group;
    for (const auto& move : moved) {
        const size_t root = move.before_net != kNoNet
                                ? dsu.Find(move.before_net)
                                : (move.after_net != kNoNet ? dsu.Find(a_count + move.after_net)
                                                            : kNoNet);
        if (root != kNoNet) {
            moved_by_group[root].push_back(move.pin);
        }
    }
    for (auto& entry : moved_by_group) {
        std::sort(entry.second.begin(), entry.second.end());
        entry.second.erase(std::unique(entry.second.begin(), entry.second.end()),
                           entry.second.end());
    }

    for (const auto& entry : before_groups) {
        const auto after_it = after_groups.find(entry.first);
        if (after_it == after_groups.end()) {
            continue;  // every pin of these nets vanished; that is a removal
        }
        const std::vector<size_t>& a_nets = entry.second;
        const std::vector<size_t>& b_nets = after_it->second;
        const bool merge = a_nets.size() > 1 && b_nets.size() == 1;
        const bool split = a_nets.size() == 1 && b_nets.size() > 1;
        if (!merge && !split) {
            continue;
        }

        std::vector<std::string> pins;
        const auto pins_it = moved_by_group.find(entry.first);
        if (pins_it != moved_by_group.end()) {
            pins = pins_it->second;
        }

        Change change;
        change.significance = Significance::kSignificant;  // always (03 §6)
        if (merge) {
            std::vector<std::string> names;
            int power_nets = 0;
            for (size_t i : a_nets) {
                names.push_back(before.nets[i].name);
                if (before.nets[i].is_power) {
                    ++power_nets;
                }
            }
            std::sort(names.begin(), names.end());
            const std::string after_name = after.nets[b_nets.front()].name;
            // Shorting two supplies together is the catastrophic case (03 §6).
            change.critical = power_nets > 1;
            change.message = "Nets " + JoinNames(names) + " merged into " +
                             (after_name.empty() ? "(unnamed)" : after_name) + JoinPins(pins);
            change.payload = NetMerged{std::move(names), after_name, std::move(pins)};
        } else {
            std::vector<std::string> names;
            for (size_t j : b_nets) {
                names.push_back(after.nets[j].name);
            }
            std::sort(names.begin(), names.end());
            const std::string before_name = before.nets[a_nets.front()].name;
            change.message = "Net " + (before_name.empty() ? "(unnamed)" : before_name) +
                             " split into " + JoinNames(names) + JoinPins(pins);
            change.payload = NetSplit{before_name, std::move(names), std::move(pins)};
        }
        out.push_back(std::move(change));
    }
}

// ---------------------------------------------------------------------------
// Step 7 — the gate (03 §8)
// ---------------------------------------------------------------------------

// `ignore_change_types` is applied to both branches. 02 §4 describes the list
// as types "to not gate on", and a type the user has excluded from gating
// should not fail the build under `fail_on: any` either; the pseudocode in
// 03 §8 mentions it only on the `significant` branch, which reads as shorthand
// rather than an intended asymmetry.
GateResult EvaluateGate(const std::vector<Change>& changes, const DiffConfig& config) {
    if (config.gate.fail_on == DiffConfig::Gate::FailOn::kNever) {
        return GateResult::kPass;
    }
    const auto& ignored = config.gate.ignore_change_types;
    int gating = 0;
    for (const auto& change : changes) {
        if (std::find(ignored.begin(), ignored.end(), ToString(change.type())) !=
            ignored.end()) {
            continue;
        }
        if (config.gate.fail_on == DiffConfig::Gate::FailOn::kAny ||
            change.significance == Significance::kSignificant) {
            ++gating;
        }
    }
    return gating > 0 ? GateResult::kFail : GateResult::kPass;
}

}  // namespace

// Iterative glob: '*' matches any run, '?' exactly one character. The
// backtracking pair makes it linear-ish rather than exponential on "a*a*a*".
bool GlobMatch(const std::string& pattern, const std::string& text) {
    size_t p = 0;
    size_t t = 0;
    size_t star = std::string::npos;
    size_t match = 0;
    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
            ++p;
            ++t;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            match = t;
        } else if (star != std::string::npos) {
            p = star + 1;
            t = ++match;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') {
        ++p;
    }
    return p == pattern.size();
}

DiffResult Diff(const ConnectivityGraph& before, const ConnectivityGraph& after,
                const DiffConfig& config) {
    DiffResult result;
    result.before_ref = before.source.revision;
    result.after_ref = after.source.revision;

    // Step 1. Skipped entirely when nothing is configured, so the common path
    // does not pay for copying two graphs.
    ConnectivityGraph normalized_before;
    ConnectivityGraph normalized_after;
    const bool normalize = NeedsNormalization(config);
    if (normalize) {
        normalized_before = Normalize(before, config);
        normalized_after = Normalize(after, config);
    }
    const ConnectivityGraph& a = normalize ? normalized_before : before;
    const ConnectivityGraph& b = normalize ? normalized_after : after;

    const ComponentMatching components = MatchComponents(a, b, result.changes);
    const NetMatching nets = MatchNets(a, b, config, result.changes);
    const std::vector<MovedPin> moved = EmitPinChanges(a, b, components, nets, result.changes);
    DetectMergesAndSplits(a, b, moved, result.changes);

    SortChanges(result.changes);
    RecomputeCounts(result);
    result.summary.gate = EvaluateGate(result.changes, config);
    return result;
}

}  // namespace netdiff
