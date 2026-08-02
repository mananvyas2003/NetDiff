#pragma once

// `.netdiff.yml` loading — 02_DATA_MODEL.md §4, 04_CLI_CI_SPEC.md §4.

#include <string>
#include <vector>

#include "netdiff/diff.hpp"

namespace netdiff {
namespace cli {

struct ConfigLoadResult {
    DiffConfig config;
    bool ok = true;
    std::string error;                 // set when ok == false
    std::vector<std::string> warnings; // unknown keys, odd values
    std::string path;                  // file actually used, empty for defaults
};

// Parse the documented subset of `.netdiff.yml` from `text`.
ConfigLoadResult ParseConfig(const std::string& text, const std::string& path);

// 04 §4 resolution order: `explicit_path` if non-empty, else the nearest
// `.netdiff.yml` from `start_dir` upwards, else built-in defaults.
ConfigLoadResult LoadConfig(const std::string& explicit_path, const std::string& start_dir);

bool ParseFailOn(const std::string& text, DiffConfig::Gate::FailOn* fail_on);

}  // namespace cli
}  // namespace netdiff
