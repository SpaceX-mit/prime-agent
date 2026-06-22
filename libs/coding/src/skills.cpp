// libs/coding/src/skills.cpp
#include "pi_coding/skills.hpp"

#include "pi_core/file_io.hpp"
#include "pi_core/path.hpp"

#include <cstdlib>
#include <set>

namespace pi::coding {

namespace {

constexpr size_t kMaxName = 64;
constexpr size_t kMaxDescription = 1024;

std::string agent_dir_default() {
    if (auto v = std::getenv("PI_AGENT_DIR"); v && *v) return v;
    if (auto h = pi::core::path::home_dir(); h) return *h + "/.pi/agent";
    return "/tmp/.pi/agent";
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b-1] == ' ' || s[b-1] == '\t' || s[b-1] == '\r' || s[b-1] == '\n')) --b;
    return s.substr(a, b - a);
}

std::string strip_quotes(const std::string& s) {
    if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') ||
                          (s.front() == '\'' && s.back() == '\'')))
        return s.substr(1, s.size() - 2);
    return s;
}

// Parse a minimal YAML frontmatter block delimited by leading/trailing "---".
// Only flat "key: value" pairs are supported (sufficient for skill metadata).
// Returns true if a frontmatter block was present.
bool parse_frontmatter(const std::string& content,
                       std::vector<std::pair<std::string, std::string>>& kv) {
    // Must start with --- on the first line.
    size_t i = 0;
    // Skip a UTF-8 BOM if present.
    if (content.size() >= 3 && (unsigned char)content[0] == 0xEF &&
        (unsigned char)content[1] == 0xBB && (unsigned char)content[2] == 0xBF) i = 3;
    if (content.compare(i, 3, "---") != 0) return false;
    // Move to end of the opening --- line.
    size_t nl = content.find('\n', i);
    if (nl == std::string::npos) return false;
    size_t pos = nl + 1;
    while (pos < content.size()) {
        size_t eol = content.find('\n', pos);
        std::string line = content.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
        std::string t = trim(line);
        if (t == "---" || t == "...") return true;  // end of frontmatter
        auto colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = trim(line.substr(0, colon));
            std::string val = strip_quotes(trim(line.substr(colon + 1)));
            if (!key.empty()) kv.emplace_back(key, val);
        }
        if (eol == std::string::npos) break;
        pos = eol + 1;
    }
    return true;  // ran off the end; treat what we got as the block
}

bool valid_name(const std::string& n) {
    if (n.empty() || n.size() > kMaxName) return false;
    if (n.front() == '-' || n.back() == '-') return false;
    if (n.find("--") != std::string::npos) return false;
    for (char c : n)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
    return true;
}

// If `dir` directly contains SKILL.md, load it as one skill. Otherwise recurse
// one level into immediate subdirectories looking for <sub>/SKILL.md.
void load_from_skills_root(const std::string& root,
                           std::vector<Skill>& out,
                           std::set<std::string>& seen_names) {
    if (!pi::core::file::is_directory(root)) return;

    auto try_skill_md = [&](const std::string& dir) {
        std::string md = dir + "/SKILL.md";
        if (!pi::core::file::exists(md)) return;
        auto r = pi::core::file::read(md);
        if (!r) return;
        std::vector<std::pair<std::string, std::string>> kv;
        if (!parse_frontmatter(r.value(), kv)) return;
        Skill s;
        s.file_path = md;
        s.base_dir = dir;
        for (auto& [k, v] : kv) {
            if (k == "name") s.name = v;
            else if (k == "description") s.description = v;
            else if (k == "disable-model-invocation")
                s.disable_model_invocation = (v == "true" || v == "1" || v == "yes");
        }
        if (!valid_name(s.name)) return;
        if (s.description.empty() || s.description.size() > kMaxDescription) return;
        if (!seen_names.insert(s.name).second) return;  // dedup by name
        out.push_back(std::move(s));
    };

    // Direct SKILL.md.
    try_skill_md(root);

    // One level of subdirectories (each a skill root).
    auto files = pi::core::file::list_files_recursive(root, {}, 2);
    if (files) {
        std::set<std::string> subdirs;
        for (auto& f : files.value()) {
            // Look for ".../SKILL.md" exactly one level under root.
            auto slash = f.find_last_of('/');
            if (slash == std::string::npos) continue;
            if (f.substr(slash + 1) != "SKILL.md") continue;
            std::string dir = f.substr(0, slash);
            if (dir != root) subdirs.insert(dir);
        }
        for (auto& d : subdirs) try_skill_md(d);
    }
}

std::string escape_xml(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            default: o.push_back(c);
        }
    }
    return o;
}

}  // namespace

std::vector<Skill> load_skills(const std::string& cwd, const std::string& agent_dir) {
    std::string adir = agent_dir.empty() ? agent_dir_default() : agent_dir;
    std::string base = cwd.empty() ? "." : cwd;

    std::vector<Skill> out;
    std::set<std::string> seen;
    load_from_skills_root(adir + "/skills", out, seen);       // global
    load_from_skills_root(base + "/.pi/skills", out, seen);   // project
    return out;
}

std::string format_skills_for_prompt(const std::vector<Skill>& skills) {
    std::vector<const Skill*> visible;
    for (auto& s : skills) if (!s.disable_model_invocation) visible.push_back(&s);
    if (visible.empty()) return "";

    std::string p =
        "\n\nThe following skills provide specialized instructions for specific tasks.\n"
        "Use the read tool to load a skill's file when the task matches its description.\n"
        "When a skill file references a relative path, resolve it against the skill "
        "directory (dirname of SKILL.md) and use that absolute path in tool commands.\n\n"
        "<available_skills>";
    for (auto* s : visible) {
        p += "\n  <skill>";
        p += "\n    <name>" + escape_xml(s->name) + "</name>";
        p += "\n    <description>" + escape_xml(s->description) + "</description>";
        p += "\n    <location>" + escape_xml(s->file_path) + "</location>";
        p += "\n  </skill>";
    }
    p += "\n</available_skills>";
    return p;
}

}  // namespace pi::coding
