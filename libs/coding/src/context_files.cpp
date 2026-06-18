// libs/coding/src/context_files.cpp
#include "pi_coding/context_files.hpp"

#include "pi_core/file_io.hpp"
#include "pi_core/path.hpp"

#include <cstdlib>
#include <ctime>
#include <set>

namespace pi::coding {

namespace {

std::string agent_dir_default() {
    if (auto v = std::getenv("PI_AGENT_DIR"); v && *v) return v;
    if (auto h = pi::core::path::home_dir(); h) return *h + "/.pi/agent";
    return "/tmp/.pi/agent";
}

// Normalize a path to an absolute, '/'-separated form (best-effort; we don't
// resolve symlinks — only collapse to absolute relative to cwd).
std::string absolutize(const std::string& p) {
    if (!p.empty() && p[0] == '/') return p;
    auto cwd = pi::core::path::current_working_dir().value_or(".");
    return cwd + "/" + p;
}

// Return the parent directory of an absolute path, or "" at root.
std::string parent_of(const std::string& dir) {
    if (dir == "/" || dir.empty()) return "";
    auto slash = dir.find_last_of('/');
    if (slash == std::string::npos) return "";
    if (slash == 0) return "/";  // parent of "/foo" is "/"
    return dir.substr(0, slash);
}

// First existing AGENTS.md/CLAUDE.md (case variants) in `dir`.
bool load_from_dir(const std::string& dir, ContextFile& out) {
    static const char* kCandidates[] = {"AGENTS.md", "AGENTS.MD", "CLAUDE.md", "CLAUDE.MD"};
    for (const char* name : kCandidates) {
        std::string fp = dir + "/" + name;
        if (pi::core::file::exists(fp)) {
            auto r = pi::core::file::read(fp);
            if (r) { out.path = fp; out.content = r.value(); return true; }
        }
    }
    return false;
}

}  // namespace

std::vector<ContextFile> load_project_context_files(const std::string& cwd,
                                                    const std::string& agent_dir) {
    std::string adir = agent_dir.empty() ? agent_dir_default() : agent_dir;
    std::string start = absolutize(cwd.empty() ? "." : cwd);

    std::vector<ContextFile> result;
    std::set<std::string> seen;

    // Global agent-dir context file first.
    {
        ContextFile cf;
        if (load_from_dir(adir, cf) && seen.insert(cf.path).second)
            result.push_back(std::move(cf));
    }

    // Walk cwd → root, collecting; ancestors ordered root→cwd.
    std::vector<ContextFile> ancestors;
    std::string dir = start;
    while (!dir.empty()) {
        ContextFile cf;
        if (load_from_dir(dir, cf) && seen.insert(cf.path).second)
            ancestors.insert(ancestors.begin(), std::move(cf));  // unshift
        if (dir == "/") break;
        std::string parent = parent_of(dir);
        if (parent == dir || parent.empty()) break;
        dir = parent;
    }
    for (auto& cf : ancestors) result.push_back(std::move(cf));
    return result;
}

std::string build_system_prompt(const std::string& cwd,
                                const std::vector<ContextFile>& context_files) {
    // Base coding-agent instructions (concise; mirrors upstream's spirit).
    std::string p =
        "You are pi, an AI coding agent operating in a terminal. You help the "
        "user with software engineering tasks using the available tools "
        "(bash, read, write, edit).\n\n"
        "Guidelines:\n"
        "- Be concise and direct in your responses.\n"
        "- Read files before editing them; match existing code style.\n"
        "- Show file paths clearly when working with files.\n"
        "- Verify your changes (build/tests) when possible.\n"
        "- Do not make destructive changes without being asked.";

    if (!context_files.empty()) {
        p += "\n\n<project_context>\n\n";
        p += "Project-specific instructions and guidelines:\n\n";
        for (const auto& cf : context_files) {
            p += "<project_instructions path=\"" + cf.path + "\">\n";
            p += cf.content;
            p += "\n</project_instructions>\n\n";
        }
        p += "</project_context>\n";
    }

    // Date + working directory footer.
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char date[16];
    std::strftime(date, sizeof(date), "%Y-%m-%d", &tm);
    p += "\nCurrent date: ";
    p += date;
    p += "\nCurrent working directory: ";
    p += (cwd.empty() ? std::string(".") : cwd);
    return p;
}

}  // namespace pi::coding
