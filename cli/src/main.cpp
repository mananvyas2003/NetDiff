// netdiff CLI — 04_CLI_CI_SPEC.md.
//
// Exit codes are a contract CI depends on (§1.3):
//   0 gate PASS, 1 gate FAIL, 2 usage error, 3 parse/resolve error, 4 internal.
// Nothing here knows anything about connectivity; it calls libnetdiff and
// prints what comes back (01 §5, surface contract).

#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "config.hpp"
#include "format.hpp"
#include "git.hpp"
#include "netdiff/diff.hpp"

namespace fs = std::filesystem;

namespace {

constexpr int kExitPass = 0;
constexpr int kExitGateFail = 1;
constexpr int kExitUsage = 2;
constexpr int kExitParse = 3;
constexpr int kExitInternal = 4;

constexpr const char* kVersion = "0.1.0";

const char* kUsage =
    "netdiff — semantic diff for KiCad schematics\n"
    "\n"
    "Usage:\n"
    "  netdiff diff <before> <after> [options]\n"
    "  netdiff diff --git <ref_a> <ref_b> [path] [options]\n"
    "  netdiff diff --staged [path] [options]\n"
    "  netdiff graph <schematic> [options]\n"
    "  netdiff validate [path]\n"
    "  netdiff version\n"
    "\n"
    "Options:\n"
    "  --format <text|json|markdown|sarif>   default: text\n"
    "  --output <file>                       default: stdout\n"
    "  --config <path>                       default: ./.netdiff.yml if present\n"
    "  --fail-on <significant|any|never>     override the config gate\n"
    "  --no-color                            plain text output\n"
    "  --quiet                               errors only\n"
    "  --include-cosmetic                    show cosmetic changes too\n"
    "\n"
    "Exit codes: 0 pass, 1 gate fail, 2 usage, 3 parse error, 4 internal.\n";

struct Options {
    netdiff::cli::OutputFormat format = netdiff::cli::OutputFormat::kText;
    std::string output;
    std::string config_path;
    std::string fail_on;
    bool no_color = false;
    bool quiet = false;
    bool include_cosmetic = false;
    bool use_git = false;
    bool staged = false;
    std::vector<std::string> positional;
};

void Fail(const std::string& message) {
    std::cerr << "netdiff: error: " << message << "\n";
}

// A scratch directory that removes itself, used for git checkouts.
class TempDir {
public:
    TempDir() {
        std::random_device device;
        std::mt19937_64 rng(device());
        for (int attempt = 0; attempt < 64; ++attempt) {
            const fs::path candidate = fs::temp_directory_path() /
                                       ("netdiff-" + std::to_string(rng()));
            std::error_code ec;
            if (fs::create_directory(candidate, ec)) {
                path_ = candidate;
                return;
            }
        }
    }
    ~TempDir() {
        if (!path_.empty()) {
            std::error_code ec;
            fs::remove_all(path_, ec);
        }
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    bool ok() const { return !path_.empty(); }
    fs::path sub(const std::string& name) const {
        const fs::path child = path_ / name;
        std::error_code ec;
        fs::create_directories(child, ec);
        return child;
    }

private:
    fs::path path_;
};

bool NeedsValue(const std::string& flag) {
    return flag == "--format" || flag == "--output" || flag == "--config" ||
           flag == "--fail-on";
}

bool ParseArgs(const std::vector<std::string>& args, Options* options, std::string* error) {
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg.rfind("--", 0) != 0) {
            options->positional.push_back(arg);
            continue;
        }
        std::string flag = arg;
        std::string value;
        bool has_value = false;
        const auto equals = arg.find('=');
        if (equals != std::string::npos) {
            flag = arg.substr(0, equals);
            value = arg.substr(equals + 1);
            has_value = true;
        } else if (NeedsValue(arg)) {
            if (i + 1 >= args.size()) {
                *error = flag + " requires a value";
                return false;
            }
            value = args[++i];
            has_value = true;
        }

        if (flag == "--format") {
            if (!netdiff::cli::ParseOutputFormat(value, &options->format)) {
                *error = "unknown --format '" + value + "' (text|json|markdown|sarif)";
                return false;
            }
        } else if (flag == "--output") {
            options->output = value;
        } else if (flag == "--config") {
            options->config_path = value;
        } else if (flag == "--fail-on") {
            netdiff::DiffConfig::Gate::FailOn parsed;
            if (!netdiff::cli::ParseFailOn(value, &parsed)) {
                *error = "unknown --fail-on '" + value + "' (significant|any|never)";
                return false;
            }
            options->fail_on = value;
        } else if (flag == "--no-color") {
            options->no_color = true;
        } else if (flag == "--quiet") {
            options->quiet = true;
        } else if (flag == "--include-cosmetic") {
            options->include_cosmetic = true;
        } else if (flag == "--git") {
            options->use_git = true;
        } else if (flag == "--staged") {
            options->staged = true;
        } else if (flag == "--help") {
            *error = "--help";
            return false;
        } else {
            *error = "unknown option " + flag;
            return false;
        }
        (void)has_value;
    }
    return true;
}

bool Emit(const std::string& text, const Options& options) {
    if (options.output.empty()) {
        std::cout << text;
        return true;
    }
    std::ofstream out(options.output, std::ios::binary);
    if (!out) {
        Fail("cannot write to " + options.output);
        return false;
    }
    out << text;
    return true;
}

netdiff::ConnectivityGraph BuildOrThrow(const std::string& entry, const std::string& revision) {
    netdiff::ProjectInput input;
    input.entry_file = entry;
    input.revision = revision;
    return netdiff::BuildGraph(input);
}

int RunDiff(Options& options) {
    std::string before_entry;
    std::string after_entry;
    std::string before_label;
    std::string after_label;
    // Where to start looking for .netdiff.yml. This is the project as the user
    // named it, never a temporary git checkout — the config lives in the
    // repository, and walking up from a temp directory would never find it.
    std::string config_start_dir;
    TempDir temp;

    if (options.use_git || options.staged) {
        netdiff::cli::GitError error;
        std::string path;
        std::string ref_a;
        std::string ref_b;

        if (options.use_git) {
            if (options.positional.size() < 3 || options.positional.size() > 4) {
                Fail("diff --git needs <ref_a> <ref_b> [path]");
                return kExitUsage;
            }
            ref_a = options.positional[1];
            ref_b = options.positional[2];
            path = options.positional.size() == 4 ? options.positional[3] : std::string(".");
        } else {
            if (options.positional.size() > 2) {
                Fail("diff --staged takes at most one path");
                return kExitUsage;
            }
            ref_a = "HEAD";
            path = options.positional.size() == 2 ? options.positional[1] : std::string(".");
        }

        const std::string entry = netdiff::cli::ResolveEntryFile(path, &error);
        if (!error.ok) {
            Fail(error.message);
            return kExitUsage;
        }
        const std::string root = netdiff::cli::RepositoryRoot(entry, &error);
        if (!error.ok) {
            Fail(error.message);
            return kExitUsage;
        }
        config_start_dir = fs::path(entry).parent_path().string();
        if (!temp.ok()) {
            Fail("cannot create a temporary directory");
            return kExitInternal;
        }

        before_entry = netdiff::cli::CheckoutProjectAt(root, ref_a, entry,
                                                       temp.sub("before").string(), &error);
        if (!error.ok) {
            Fail(error.message);
            return kExitParse;
        }
        before_label = ref_a;

        if (options.staged) {
            // Working tree as it stands (04 §1.5).
            after_entry = entry;
            after_label = "working-tree";
        } else {
            after_entry = netdiff::cli::CheckoutProjectAt(root, ref_b, entry,
                                                          temp.sub("after").string(), &error);
            if (!error.ok) {
                Fail(error.message);
                return kExitParse;
            }
            after_label = ref_b;
        }
    } else {
        if (options.positional.size() != 3) {
            Fail("diff needs <before> <after> (or --git / --staged)");
            return kExitUsage;
        }
        netdiff::cli::GitError error;
        before_entry = netdiff::cli::ResolveEntryFile(options.positional[1], &error);
        if (!error.ok) {
            Fail(error.message);
            return kExitUsage;
        }
        after_entry = netdiff::cli::ResolveEntryFile(options.positional[2], &error);
        if (!error.ok) {
            Fail(error.message);
            return kExitUsage;
        }
        before_label = before_entry;
        after_label = after_entry;
        config_start_dir = fs::path(after_entry).parent_path().string();
    }

    // Config is resolved next to the design being diffed (04 §4).
    netdiff::cli::ConfigLoadResult loaded =
        netdiff::cli::LoadConfig(options.config_path, config_start_dir);
    if (!loaded.ok) {
        Fail(loaded.error);
        return kExitUsage;
    }
    if (!options.quiet) {
        for (const auto& warning : loaded.warnings) {
            std::cerr << "netdiff: warning: " << warning << "\n";
        }
    }
    if (!options.fail_on.empty()) {
        // A flag beats the file (04 §4).
        netdiff::cli::ParseFailOn(options.fail_on, &loaded.config.gate.fail_on);
    }

    netdiff::ConnectivityGraph before;
    netdiff::ConnectivityGraph after;
    try {
        before = BuildOrThrow(before_entry, before_label);
        after = BuildOrThrow(after_entry, after_label);
    } catch (const std::exception& ex) {
        // The engine names the offending file in its message (04 �5).
        Fail(ex.what());
        return kExitParse;
    }

    if (before.schema_version != after.schema_version) {
        // 02 §5: refuse to compare incompatible graph schemas.
        Fail("incompatible graph schema versions: " + before.schema_version + " vs " +
             after.schema_version);
        return kExitUsage;
    }

    const netdiff::DiffResult result = netdiff::Diff(before, after, loaded.config);

    netdiff::cli::FormatOptions format_options;
    format_options.color = !options.no_color && options.output.empty();
    format_options.include_cosmetic = options.include_cosmetic;
    format_options.tool_version = kVersion;
    format_options.artifact_uri = after_entry;

    if (!options.quiet || options.format != netdiff::cli::OutputFormat::kText) {
        if (!Emit(netdiff::cli::FormatDiff(result, options.format, format_options), options)) {
            return kExitInternal;
        }
    }
    return result.summary.gate == netdiff::GateResult::kFail ? kExitGateFail : kExitPass;
}

int RunGraph(const Options& options) {
    if (options.positional.size() != 2) {
        Fail("graph needs <schematic>");
        return kExitUsage;
    }
    netdiff::cli::GitError error;
    const std::string entry = netdiff::cli::ResolveEntryFile(options.positional[1], &error);
    if (!error.ok) {
        Fail(error.message);
        return kExitUsage;
    }
    try {
        const netdiff::ConnectivityGraph graph = BuildOrThrow(entry, "working-tree");
        if (!Emit(netdiff::SerializeGraphJson(graph), options)) {
            return kExitInternal;
        }
    } catch (const std::exception& ex) {
        // The engine names the offending file in its message (04 �5).
        Fail(ex.what());
        return kExitParse;
    }
    return kExitPass;
}

int RunValidate(const Options& options) {
    const std::string path = options.positional.size() >= 2 ? options.positional[1] : ".";
    netdiff::cli::GitError error;
    const std::string entry = netdiff::cli::ResolveEntryFile(path, &error);
    if (!error.ok) {
        Fail(error.message);
        return kExitUsage;
    }
    try {
        const netdiff::ConnectivityGraph graph = BuildOrThrow(entry, "working-tree");
        if (!options.quiet) {
            std::cout << "ok: " << entry << "\n"
                      << "  sheets=" << graph.source.sheet_files.size()
                      << " components=" << graph.stats.component_count
                      << " nets=" << graph.stats.net_count
                      << " pins=" << graph.stats.pin_count << "\n";
        }
    } catch (const std::exception& ex) {
        Fail(ex.what());
        return kExitParse;
    }
    return kExitPass;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::vector<std::string> args(argv + 1, argv + argc);
        if (args.empty()) {
            std::cerr << kUsage;
            return kExitUsage;
        }

        Options options;
        std::string error;
        if (!ParseArgs(args, &options, &error)) {
            if (error == "--help") {
                std::cout << kUsage;
                return kExitPass;
            }
            Fail(error);
            std::cerr << "\n" << kUsage;
            return kExitUsage;
        }
        if (options.positional.empty()) {
            Fail("no command given");
            std::cerr << "\n" << kUsage;
            return kExitUsage;
        }

        const std::string& command = options.positional[0];
        if (command == "version") {
            std::cout << "netdiff " << kVersion << "\n";
            return kExitPass;
        }
        if (command == "help") {
            std::cout << kUsage;
            return kExitPass;
        }
        if (command == "diff") {
            return RunDiff(options);
        }
        if (command == "graph") {
            return RunGraph(options);
        }
        if (command == "validate") {
            return RunValidate(options);
        }
        Fail("unknown command '" + command + "'");
        std::cerr << "\n" << kUsage;
        return kExitUsage;
    } catch (const std::exception& ex) {
        std::cerr << "netdiff: internal error: " << ex.what() << "\n";
        return kExitInternal;
    } catch (...) {
        std::cerr << "netdiff: internal error\n";
        return kExitInternal;
    }
}
