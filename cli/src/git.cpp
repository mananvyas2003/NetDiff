#include "git.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#define NETDIFF_POPEN _popen
#define NETDIFF_PCLOSE _pclose
#else
#include <sys/wait.h>
#define NETDIFF_POPEN popen
#define NETDIFF_PCLOSE pclose
#endif

namespace netdiff {
namespace cli {
namespace {

namespace fs = std::filesystem;

struct CommandResult {
    int exit_code = -1;
    std::string output;
};

std::string Quote(const std::string& text) {
    return "\"" + text + "\"";
}

std::string TrimTrailingNewlines(std::string text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
    return text;
}

// Collapse ".", ".." and redundant separators so `git -C` never sees paths like
// `/tmp/repo/.` (rejected on Linux CI even when the directory is a valid repo).
fs::path NormalizeFsPath(fs::path path) {
    std::error_code ec;
    fs::path absolute = fs::absolute(path, ec);
    if (!ec) {
        path = absolute;
    }
    path = path.lexically_normal();
    while (path.has_filename() && path.filename() == ".") {
        path = path.parent_path();
    }
    if (path.empty()) {
        path = ".";
    }
    return path;
}

fs::path DirectoryForGit(const fs::path& path) {
    std::error_code ec;
    fs::path start = NormalizeFsPath(path);
    if (fs::is_regular_file(start, ec)) {
        const fs::path parent = start.parent_path();
        if (!parent.empty()) {
            start = parent;
        }
    }
    return NormalizeFsPath(start);
}

// On Windows, open the pipe in binary mode so `git show` is not newline-
// translated. POSIX popen only accepts "r"/"w" (and optionally "e"); "rb"
// returns NULL on glibc/macOS and makes every git call look like "not a repo".
CommandResult RunCommand(const std::string& command) {
    CommandResult result;
#if defined(_WIN32)
    FILE* pipe = NETDIFF_POPEN(command.c_str(), "rb");
#else
    FILE* pipe = NETDIFF_POPEN(command.c_str(), "r");
#endif
    if (pipe == nullptr) {
        return result;
    }
    char buffer[4096];
    size_t read = 0;
    while ((read = std::fread(buffer, 1, sizeof(buffer), pipe)) > 0) {
        result.output.append(buffer, read);
    }
    const int status = NETDIFF_PCLOSE(pipe);
#if defined(_WIN32)
    result.exit_code = status;
#else
    if (status == -1) {
        result.exit_code = -1;
    } else if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else {
        result.exit_code = -1;
    }
#endif
    return result;
}

std::string GitCommand(const std::string& repo_root, const std::string& args) {
    return "git -C " + Quote(repo_root) + " " + args + " 2>" +
#if defined(_WIN32)
           std::string("NUL");
#else
           std::string("/dev/null");
#endif
}

bool IsProjectFile(const fs::path& path) {
    const std::string ext = path.extension().string();
    return ext == ".kicad_sch" || ext == ".kicad_pro" || ext == ".kicad_prl";
}

// Git speaks forward slashes regardless of platform.
std::string ToPosix(const fs::path& path) {
    std::string text = path.generic_string();
    return text;
}

}  // namespace

bool IsSafeRef(const std::string& ref) {
    if (ref.empty() || ref.size() > 256) {
        return false;
    }
    for (char c : ref) {
        const bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                             (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-' ||
                             c == '/' || c == '~' || c == '^' || c == '{' || c == '}' ||
                             c == '@';
        if (!allowed) {
            return false;
        }
    }
    // "--flag" would be read as an option, and ".." as a range.
    return ref[0] != '-';
}

std::string RepositoryRoot(const std::string& path, GitError* error) {
    const fs::path start = DirectoryForGit(path);
    const CommandResult result =
        RunCommand(GitCommand(start.string(), "rev-parse --show-toplevel"));
    if (result.exit_code != 0 || result.output.empty()) {
        error->ok = false;
        error->message = "not inside a git repository: " + start.string();
        return std::string();
    }
    return NormalizeFsPath(TrimTrailingNewlines(result.output)).string();
}

std::string ResolveEntryFile(const std::string& path, GitError* error) {
    std::error_code ec;
    const fs::path input = NormalizeFsPath(path);
    if (fs::is_regular_file(input, ec)) {
        return input.string();
    }
    if (!fs::is_directory(input, ec)) {
        error->ok = false;
        error->message = "no such file or directory: " + path;
        return std::string();
    }

    std::vector<fs::path> schematics;
    std::vector<fs::path> projects;
    for (const auto& entry : fs::directory_iterator(input, ec)) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        if (entry.path().extension() == ".kicad_sch") {
            schematics.push_back(NormalizeFsPath(entry.path()));
        } else if (entry.path().extension() == ".kicad_pro") {
            projects.push_back(NormalizeFsPath(entry.path()));
        }
    }
    std::sort(schematics.begin(), schematics.end());
    std::sort(projects.begin(), projects.end());

    // The sheet sharing a stem with the .kicad_pro is the project's root sheet.
    for (const auto& project : projects) {
        for (const auto& sheet : schematics) {
            if (sheet.stem() == project.stem()) {
                return sheet.string();
            }
        }
    }
    if (schematics.size() == 1) {
        return schematics.front().string();
    }
    error->ok = false;
    if (schematics.empty()) {
        error->message = "no .kicad_sch found in " + input.string();
    } else {
        error->message = "several .kicad_sch in " + input.string() +
                         "; name the entry sheet explicitly";
    }
    return std::string();
}

std::string CheckoutProjectAt(const std::string& repo_root, const std::string& ref,
                              const std::string& entry_path, const std::string& dest_dir,
                              GitError* error) {
    if (!IsSafeRef(ref)) {
        error->ok = false;
        error->message = "refusing unsafe git revision: " + ref;
        return std::string();
    }

    std::error_code ec;
    const fs::path root = NormalizeFsPath(repo_root);
    const fs::path entry = NormalizeFsPath(entry_path);
    const fs::path project_dir = entry.parent_path();
    const fs::path relative_dir = fs::relative(project_dir, root, ec);
    if (ec) {
        error->ok = false;
        error->message = "project is outside the repository: " + entry.string();
        return std::string();
    }
    const std::string prefix =
        (relative_dir.empty() || relative_dir == ".") ? std::string()
                                                      : ToPosix(relative_dir) + "/";

    // Every file of the project as it existed at `ref` — a hierarchical design
    // needs all its sheets, and .kicad_pro carries the bus aliases.
    const CommandResult listing = RunCommand(
        GitCommand(root.string(), "ls-tree -r --name-only " + Quote(ref)));
    if (listing.exit_code != 0) {
        error->ok = false;
        error->message = "git ls-tree failed for revision '" + ref + "'";
        return std::string();
    }

    std::vector<std::string> wanted;
    std::istringstream stream(listing.output);
    std::string line;
    while (std::getline(stream, line)) {
        line = TrimTrailingNewlines(line);
        if (line.empty()) {
            continue;
        }
        if (!prefix.empty() && line.rfind(prefix, 0) != 0) {
            continue;
        }
        if (IsProjectFile(fs::path(line))) {
            wanted.push_back(line);
        }
    }
    if (wanted.empty()) {
        error->ok = false;
        error->message = "revision '" + ref + "' contains no KiCad project files under " +
                         (prefix.empty() ? std::string("the repository root") : prefix);
        return std::string();
    }

    for (const auto& path : wanted) {
        const CommandResult blob =
            RunCommand(GitCommand(root.string(), "show " + Quote(ref + ":" + path)));
        if (blob.exit_code != 0) {
            error->ok = false;
            error->message = "git show failed for " + ref + ":" + path;
            return std::string();
        }
        const fs::path out_path = fs::path(dest_dir) / fs::path(path);
        fs::create_directories(out_path.parent_path(), ec);
        std::ofstream out(out_path, std::ios::binary);
        if (!out) {
            error->ok = false;
            error->message = "cannot write " + out_path.string();
            return std::string();
        }
        out.write(blob.output.data(), static_cast<std::streamsize>(blob.output.size()));
    }

    const fs::path relative_entry = fs::relative(entry, root, ec);
    const fs::path checked_out = fs::path(dest_dir) / relative_entry;
    if (!fs::exists(checked_out, ec)) {
        error->ok = false;
        error->message = "entry sheet " + ToPosix(relative_entry) +
                         " does not exist at revision '" + ref + "'";
        return std::string();
    }
    return checked_out.string();
}

}  // namespace cli
}  // namespace netdiff
