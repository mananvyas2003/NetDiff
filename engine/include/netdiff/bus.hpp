#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace netdiff {

// bus_alias name → member tokens (may include nested vectors like D[0..3]).
using BusAliasMap = std::unordered_map<std::string, std::vector<std::string>>;

// Expand a KiCad bus label / sheet-pin name into member net names.
//
// Rules (KiCad Schematic Editor docs + SCH_CONNECTION):
//   Vector:  PREFIX[M..N]  →  PREFIX{i} for i in [min(M,N), max(M,N)]
//   Group:   [Name]{mem1 mem2 ...}  → members (vectors expanded recursively)
//            named   USB1{DP DM} → USB1.DP, USB1.DM
//   Alias:   Name{Alias} / {Alias} resolves Alias via bus_aliases (project or sheet)
//
// Non-bus names return a single-element vector containing the name itself.
// Empty / unparseable bus syntax returns {}.
std::vector<std::string> ExpandBusMembers(const std::string& name);
std::vector<std::string> ExpandBusMembers(const std::string& name,
                                          const BusAliasMap& aliases);

bool IsBusName(const std::string& name);

// Split a group bus "[Name]{mem1 mem2 ...}" into its name and member list.
//
// The member list is the brace group that *ends* the name, found by matching
// backwards — KiCad text markup (~{} overbar, _{} subscript, ^{} superscript)
// is part of the name, so "PCIe_{C5x4}{PCIe}" is the group "PCIe_{C5x4}" with
// member list "PCIe", and "~{RESET}" is a plain net name, not a group.
// Returns false (leaving the outputs untouched) if `name` is not a group bus.
bool ParseBusGroup(const std::string& name, std::string* group_name,
                   std::string* interior);

// Load "bus_aliases" from a KiCad project file (.kicad_pro). Returns {} if missing.
BusAliasMap LoadBusAliasesFromProject(const std::string& entry_schematic_path);

}  // namespace netdiff
