// libs/coding/src/tools/find.cpp
#include "pi_coding/tools/find_tool.hpp"

#include "pi_core/path.hpp"

#include <dirent.h>
#include <fnmatch.h>
#include <sys/stat.h>

namespace pi::coding::tools {

namespace {

bool match_glob(const std::string& name, const std::string& pattern) {
    if (pattern.empty()) return true;
    // Support ** in middle of pattern by falling back to fnmatch on each path segment.
    // For simplicity, treat ** like *.
    std::string translated;
    translated.reserve(pattern.size());
    for (size_t i = 0; i < pattern.size(); ++i) {
        if (i + 1 < pattern.size() && pattern[i] == '*' && pattern[i+1] == '*') {
            translated += '*';
            ++i;  // skip the second *
        } else {
            translated += pattern[i];
        }
    }
    return fnmatch(translated.c_str(), name.c_str(), 0) == 0;
}

void walk(const std::string& base, const std::string& pattern,
          bool only_dir, bool only_file,
          std::vector<std::string>& out, size_t limit) {
    if (out.size() >= limit) return;
    DIR* d = ::opendir(base.c_str());
    if (!d) return;
    struct dirent* ent;
    while ((ent = ::readdir(d))) {
        std::string name = ent->d_name;
        if (name == "." || name == "..") continue;
        if (name == ".git") continue;
        std::string full = base + "/" + name;
        struct stat st{};
        if (::lstat(full.c_str(), &st) != 0) continue;
        bool is_dir = (st.st_mode & S_IFDIR) != 0;
        bool is_file = (st.st_mode & S_IFREG) != 0;
        if ((is_dir && only_dir) || (is_file && only_file)) {
            if (match_glob(name, pattern)) {
                out.push_back(full);
                if (out.size() >= limit) { ::closedir(d); return; }
            }
        }
        if (is_dir) {
            walk(full, pattern, only_dir, only_file, out, limit);
            if (out.size() >= limit) { ::closedir(d); return; }
        }
    }
    ::closedir(d);
}

}  // namespace

FindTool::FindTool(std::string cwd) : cwd_(std::move(cwd)) {}

pi::core::Json FindTool::parameters() const {
    return pi::core::Json{
        {"type", "object"},
        {"properties", {
            {"pattern", {{"type", "string"}, {"description", "Glob pattern (supports **)"}}},
            {"path", {{"type", "string"}, {"description", "Search root (default cwd)"}}},
            {"type", {{"type", "string"}, {"description", "file or directory"}}},
            {"limit", {{"type", "number"}, {"description", "Max results"}}}
        }},
        {"required", {"pattern"}}
    };
}

pi::agent::ToolResult FindTool::execute(
    const pi::core::Json& args,
    pi::agent::AbortSignal& /*signal*/,
    pi::agent::ProgressFn /*on_update*/) {

    pi::agent::ToolResult r;
    if (!args.contains("pattern") || !args["pattern"].is_string()) {
        pi::ai::TextContent t; t.text = "Error: missing required argument 'pattern'";
        r.content = {t}; r.is_error = true; return r;
    }
    std::string pattern = args["pattern"].get<std::string>();
    std::string path = args.value("path", cwd_);
    std::string type = args.value("type", std::string{"file"});
    size_t limit = static_cast<size_t>(args.value("limit", 200));
    if (limit == 0 || limit > kMaxResults) limit = kMaxResults;

    bool only_file = (type == "file");
    bool only_dir = (type == "directory");

    std::vector<std::string> out;
    walk(path, pattern, only_dir, only_file, out, limit);

    std::ostringstream o;
    o << "Found " << out.size() << " result" << (out.size() == 1 ? "" : "s")
      << " in " << path << " matching '" << pattern << "'\n---\n";
    for (auto& p : out) o << p << "\n";

    pi::ai::TextContent t; t.text = o.str();
    r.content = {t};
    r.details = pi::core::Json{{"count", out.size()}};
    return r;
}

}  // namespace pi::coding::tools
