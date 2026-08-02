#pragma once

// Git plumbing for `--git` / `--staged` — 04_CLI_CI_SPEC.md §1.5.
//
// Lives in the CLI surface. libnetdiff never shells out; 01 §6 permits a
// surface to carry the per-platform bits the core may not.

#include <string>
#include <vector>

namespace netdiff {
namespace cli {

struct GitError {
    bool ok = true;
    std::string message;
};

// Refs are pasted into a command line, so anything outside a conservative
// charset is rejected rather than quoted-and-hoped.
bool IsSafeRef(const std::string& ref);

// Absolute path of the repository containing `path`, or empty if none.
std::string RepositoryRoot(const std::string& path, GitError* error);

// Materialise every project file (.kicad_sch/.kicad_pro/.kicad_prl) that
// existed at `ref` under the entry's directory into `dest_dir`, preserving
// relative layout. Returns the path of the entry file inside `dest_dir`.
// A hierarchical project needs all of its sheets, not just the entry (§1.5).
std::string CheckoutProjectAt(const std::string& repo_root, const std::string& ref,
                              const std::string& entry_path, const std::string& dest_dir,
                              GitError* error);

// If `path` is a directory, choose the project entry inside it: the .kicad_sch
// matching a .kicad_pro stem, else the only .kicad_sch present.
std::string ResolveEntryFile(const std::string& path, GitError* error);

}  // namespace cli
}  // namespace netdiff
