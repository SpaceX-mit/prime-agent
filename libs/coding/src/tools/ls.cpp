// libs/coding/src/tools/ls.cpp
#include "pi_coding/tools/ls_tool.hpp"

#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
#include <sstream>

namespace pi::coding::tools {

namespace {

struct Entry {
    std::string name;
    bool is_dir = false;
    int64_t size = 0;
};

}  // namespace

LsTool::LsTool(std::string cwd) : cwd_(std::move(cwd)) {}

pi::core::Json LsTool::parameters() const {
    return pi::core::Json{
        {"type", "object"},
        {"properties", {
            {"path", {{"type", "string"}, {"description", "Directory to list (default cwd)"}}},
            {"all", {{"type", "boolean"}, {"description", "Include hidden (default false)"}}}
        }}
    };
}

pi::agent::ToolResult LsTool::execute(
    const pi::core::Json& args,
    pi::agent::AbortSignal& /*signal*/,
    pi::agent::ProgressFn /*on_update*/) {

    pi::agent::ToolResult r;
    std::string path = args.value("path", cwd_);
    bool all = args.value("all", false);

    std::vector<Entry> entries;
    DIR* d = ::opendir(path.c_str());
    if (!d) {
        pi::ai::TextContent t; t.text = "Error: cannot open " + path;
        r.content = {t}; r.is_error = true; return r;
    }
    struct dirent* ent;
    while ((ent = ::readdir(d))) {
        std::string name = ent->d_name;
        if (!all && (name.empty() || name[0] == '.')) continue;
        if (name == "." || name == "..") continue;
        std::string full = path + "/" + name;
        struct stat st{};
        if (::lstat(full.c_str(), &st) != 0) continue;
        Entry e;
        e.name = name;
        e.is_dir = (st.st_mode & S_IFDIR) != 0;
        e.size = static_cast<int64_t>(st.st_size);
        entries.push_back(std::move(e));
    }
    ::closedir(d);

    std::sort(entries.begin(), entries.end(),
              [](const Entry& a, const Entry& b) { return a.name < b.name; });

    std::ostringstream o;
    o << "Contents of " << path << " (" << entries.size() << " entries)\n---\n";
    for (auto& e : entries) {
        if (e.is_dir) {
            o << e.name << "/\n";
        } else {
            o << e.name << "  (" << e.size << " bytes)\n";
        }
    }

    pi::ai::TextContent t; t.text = o.str();
    r.content = {t};
    pi::core::Json arr = pi::core::Json::array();
    for (auto& e : entries) {
        arr.push_back({{"name", e.name}, {"isDir", e.is_dir}, {"size", e.size}});
    }
    r.details = pi::core::Json{{"entries", arr}};
    return r;
}

}  // namespace pi::coding::tools
