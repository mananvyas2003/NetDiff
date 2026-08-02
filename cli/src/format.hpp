#pragma once

// Output formatters — 04_CLI_CI_SPEC.md §1.4.
//
// These live in the CLI surface, not in libnetdiff: 01_ARCHITECTURE.md §2.3
// gives the CLI "argument parsing, Git plumbing, output formatting", and §5
// requires the library to stay free of presentation concerns.

#include <string>

#include "netdiff/diff.hpp"

namespace netdiff {
namespace cli {

enum class OutputFormat { kText, kJson, kMarkdown, kSarif };

// Returns false if `text` is not one of text|json|markdown|sarif.
bool ParseOutputFormat(const std::string& text, OutputFormat* format);

struct FormatOptions {
    bool color = false;
    bool include_cosmetic = false;
    std::string tool_version = "0.1.0";
    // Schematic this diff is about; becomes the SARIF result location.
    std::string artifact_uri;
};

std::string FormatDiff(const DiffResult& result, OutputFormat format,
                       const FormatOptions& options);

// "PinConnectionChanged" -> "pin-connection-changed" (SARIF ruleId, 04 §1.4).
std::string RuleIdFor(ChangeType type);

}  // namespace cli
}  // namespace netdiff
