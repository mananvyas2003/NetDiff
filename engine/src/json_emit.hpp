#pragma once

// Shared deterministic JSON emitters (internal to libnetdiff).
//
// Keys are written in lexicographic order at every object level so identical
// input yields byte-identical output. Component/Net live here rather than in
// serialize_graph.cpp because DiffResult payloads embed them verbatim.

#include <sstream>
#include <string>

#include "netdiff/graph.hpp"

namespace netdiff {
namespace json {

std::string Escape(const std::string& input);
void EmitString(std::ostringstream& out, const std::string& value);
void EmitBool(std::ostringstream& out, bool value);

// Each Emit* writes the object's opening `{` at the stream's current position
// (the caller supplies any leading indentation), its fields at `indent` plus
// two spaces, and the closing `}` at `indent`. No trailing newline.
void EmitComponent(std::ostringstream& out, const Component& component,
                   const std::string& indent);
void EmitNet(std::ostringstream& out, const Net& net, const std::string& indent);

}  // namespace json
}  // namespace netdiff
