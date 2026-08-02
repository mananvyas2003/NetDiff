#include "netdiff/diff.hpp"

#include <algorithm>
#include <string>

namespace netdiff {
namespace {

std::string Join(const std::vector<std::string>& parts) {
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i != 0) {
            out += '\x1f';
        }
        out += parts[i];
    }
    return out;
}

}  // namespace

const char* const kUnconnectedNetName = "unconnected";

const char* ToString(Significance significance) {
    return significance == Significance::kSignificant ? "SIGNIFICANT" : "COSMETIC";
}

const char* ToString(GateResult gate) {
    return gate == GateResult::kFail ? "FAIL" : "PASS";
}

const char* ToString(ChangeType type) {
    switch (type) {
    case ChangeType::kComponentAdded:      return "ComponentAdded";
    case ChangeType::kComponentRemoved:    return "ComponentRemoved";
    case ChangeType::kComponentModified:   return "ComponentModified";
    case ChangeType::kNetAdded:            return "NetAdded";
    case ChangeType::kNetRemoved:          return "NetRemoved";
    case ChangeType::kNetRenamed:          return "NetRenamed";
    case ChangeType::kNetMerged:           return "NetMerged";
    case ChangeType::kNetSplit:            return "NetSplit";
    case ChangeType::kPinConnectionChanged: return "PinConnectionChanged";
    }
    return "Unknown";
}

ChangeType Change::type() const {
    // ChangePayload alternatives are declared in ChangeType order.
    return static_cast<ChangeType>(payload.index());
}

std::string ChangeSortKey(const Change& change) {
    // The spec key (PinId / net name / ComponentId) comes first; anything after
    // a unit-separator only disambiguates otherwise-equal keys (two unnamed nets
    // share the empty name but never a net_id).
    struct Visitor {
        std::string operator()(const ComponentAdded& p) const {
            return MakeComponentId(p.component);
        }
        std::string operator()(const ComponentRemoved& p) const {
            return MakeComponentId(p.component);
        }
        std::string operator()(const ComponentModified& p) const { return p.ref; }
        std::string operator()(const NetAdded& p) const {
            return p.net.name + '\x1f' + p.net.net_id;
        }
        std::string operator()(const NetRemoved& p) const {
            return p.net.name + '\x1f' + p.net.net_id;
        }
        std::string operator()(const NetRenamed& p) const {
            return p.after_name + '\x1f' + p.before_name + '\x1f' + p.net.net_id;
        }
        std::string operator()(const NetMerged& p) const {
            return p.after_net + '\x1f' + Join(p.before_nets);
        }
        std::string operator()(const NetSplit& p) const {
            return p.before_net + '\x1f' + Join(p.after_nets);
        }
        std::string operator()(const PinConnectionChanged& p) const { return p.pin; }
    };
    return std::visit(Visitor{}, change.payload);
}

void SortChanges(std::vector<Change>& changes) {
    std::stable_sort(changes.begin(), changes.end(),
                     [](const Change& a, const Change& b) {
                         if (a.significance != b.significance) {
                             // Significance descending: SIGNIFICANT before COSMETIC.
                             return a.significance > b.significance;
                         }
                         if (a.type() != b.type()) {
                             return a.type() < b.type();
                         }
                         const std::string ka = ChangeSortKey(a);
                         const std::string kb = ChangeSortKey(b);
                         if (ka != kb) {
                             return ka < kb;
                         }
                         return a.message < b.message;
                     });
}

void RecomputeCounts(DiffResult& result) {
    result.summary.significant_count = 0;
    result.summary.cosmetic_count = 0;
    result.summary.by_type.clear();
    for (const auto& change : result.changes) {
        if (change.significance == Significance::kSignificant) {
            ++result.summary.significant_count;
        } else {
            ++result.summary.cosmetic_count;
        }
        ++result.summary.by_type[ToString(change.type())];
    }
}

}  // namespace netdiff
