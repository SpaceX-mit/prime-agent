// libs/coding/src/tools/grep.cpp
#include "pi_coding/tools/grep_tool.hpp"

#include "pi_core/path.hpp"
#include "pi_core/strutil.hpp"

#include <dirent.h>
#include <fstream>
#include <fnmatch.h>
#include <regex>
#include <sstream>
#include <sys/stat.h>

namespace pi::coding::tools {

namespace {

bool matches_include(const std::string& filename, const std::string& pattern) {
    if (pattern.empty()) return true;
    return fnmatch(pattern.c_str(), filename.c_str(), 0) == 0;
}

void walk_for_grep(const std::string& base, const std::string& include,
                   const std::regex& re, std::vector<std::string>& matches,
                   size_t limit, size_t& total) {
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
        if (is_dir) {
            walk_for_grep(full, include, re, matches, limit, total);
        } else if (is_file && matches_include(name, include)) {
            // Read file and grep.
            std::ifstream f(full, std::ios::binary);
            if (!f) continue;
            std::string line;
            int line_no = 0;
            while (std::getline(f, line)) {
                line_no++;
                if (line.size() > 4096) continue;  // skip huge lines
                if (std::regex_search(line, re)) {
                    total++;
                    if (matches.size() < limit) {
                        std::ostringstream o;
                        o << full << ":" << line_no << ":" << line;
                        matches.push_back(o.str());
                    }
                }
            }
        }
    }
    ::closedir(d);
}

}  // namespace

GrepTool::GrepTool(std::string cwd) : cwd_(std::move(cwd)) {}

pi::core::Json GrepTool::parameters() const {
    return pi::core::Json{
        {"type", "object"},
        {"properties", {
            {"pattern", {{"type", "string"}, {"description", "Regex pattern"}}},
            {"path", {{"type", "string"}, {"description", "Search root (default cwd)"}}},
            {"include", {{"type", "string"}, {"description", "Glob filter (e.g. *.cpp)"}}},
            {"limit", {{"type", "number"}, {"description", "Max matches returned"}}}
        }},
        {"required", {"pattern"}}
    };
}

pi::agent::ToolResult GrepTool::execute(
    const pi::core::Json& args,
    pi::agent::AbortSignal& signal,
    pi::agent::ProgressFn /*on_update*/) {

    pi::agent::ToolResult r;
    if (!args.contains("pattern") || !args["pattern"].is_string()) {
        pi::ai::TextContent t; t.text = "Error: missing required argument 'pattern'";
        r.content = {t}; r.is_error = true; return r;
    }
    std::string pattern = args["pattern"].get<std::string>();
    std::string path = args.value("path", cwd_);
    std::string include = args.value("include", std::string{});
    size_t limit = static_cast<size_t>(args.value("limit", 100));
    if (limit == 0 || limit > kMaxMatches) limit = kMaxMatches;

    std::regex re;
    try {
        re = std::regex(pattern);
    } catch (const std::exception& e) {
        pi::ai::TextContent t; t.text = std::string("Error: invalid regex: ") + e.what();
        r.content = {t}; r.is_error = true; return r;
    }

    std::vector<std::string> matches;
    size_t total = 0;
    walk_for_grep(path, include, re, matches, limit, total);

    std::ostringstream o;
    o << "Found " << total << " match" << (total == 1 ? "" : "es")
      << " in " << path << "\n";
    if (!include.empty()) o << "(filter: " << include << ")\n";
    o << "---\n";
    for (auto& m : matches) o << m << "\n";
    if (total > matches.size()) {
        o << "... (" << (total - matches.size()) << " more, truncated)\n";
    }

    pi::ai::TextContent t; t.text = o.str();
    r.content = {t};
    r.details = pi::core::Json{{"totalMatches", total},
                                {"returnedMatches", matches.size()},
                                {"truncated", total > matches.size()}};
    return r;
}

}  // namespace pi::coding::tools
