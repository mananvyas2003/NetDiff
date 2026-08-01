#pragma once

#include "interpreter.h"

#include <string>
#include <vector>

namespace netdiff {

struct LoadedSheet {
    std::string path;        // filesystem path to .kicad_sch
    std::string sheet_path;  // hierarchical path, e.g. "/", "/Power/"
    std::string uuid_path;   // KiCad instance path, e.g. "/root-uuid/sheet-uuid"
    Schematic schematic;
};

// Load entry schematic and recursively follow Sheetfile references.
// Throws std::runtime_error on missing files or inclusion cycles.
std::vector<LoadedSheet> LoadProjectSheets(const std::string& entry_file);

}  // namespace netdiff
