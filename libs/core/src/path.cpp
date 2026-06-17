// libs/core/src/path.cpp
#include "pi_core/path.hpp"
#include "pi_core/env.hpp"
#include "pi_core/error.hpp"

#include <pwd.h>
#include <unistd.h>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace pi::core::path {

std::optional<std::string> home_dir() {
    if (auto h = env::get("HOME"); h && !h->empty()) return h;
    if (struct passwd* pw = getpwuid(getuid())) {
        return std::string(pw->pw_dir);
    }
    return std::nullopt;
}

std::optional<std::string> xdg_config_home() {
    if (auto v = env::get("XDG_CONFIG_HOME"); v && !v->empty()) return v;
    if (auto h = home_dir(); h) return *h + "/.config";
    return std::nullopt;
}

std::optional<std::string> xdg_data_home() {
    if (auto v = env::get("XDG_DATA_HOME"); v && !v->empty()) return v;
    if (auto h = home_dir(); h) return *h + "/.local/share";
    return std::nullopt;
}

std::optional<std::string> xdg_cache_home() {
    if (auto v = env::get("XDG_CACHE_HOME"); v && !v->empty()) return v;
    if (auto h = home_dir(); h) return *h + "/.cache";
    return std::nullopt;
}

std::optional<std::string> current_working_dir() {
    char buf[PATH_MAX];
    if (getcwd(buf, sizeof(buf))) return std::string(buf);
    return std::nullopt;
}

std::string expand_home(std::string_view p) {
    if (p.empty() || p[0] != '~') return std::string(p);
    if (p.size() == 1) {
        auto h = home_dir();
        return h ? *h : std::string(p);
    }
    if (p[1] == '/' || p[1] == '\\') {
        auto h = home_dir();
        if (!h) return std::string(p);
        return *h + std::string(p.substr(1));
    }
    return std::string(p);
}

std::string join(std::string_view a, std::string_view b) {
    if (a.empty()) return std::string(b);
    if (b.empty()) return std::string(a);
    char last = a.back();
    if (last == '/' || last == '\\') {
        return std::string(a) + std::string(b);
    }
    return std::string(a) + "/" + std::string(b);
}

std::string normalize(std::string_view p, bool resolve_symlinks) {
    try {
        fs::path fp{std::string(p)};
        if (resolve_symlinks && fs::exists(fp)) {
            return fs::canonical(fp).string();
        }
        return fs::weakly_canonical(fp).string();
    } catch (...) {
        return std::string(p);
    }
}

bool is_within(std::string_view parent, std::string_view child) {
    try {
        fs::path fp = fs::weakly_canonical(std::string(parent));
        fs::path fc = fs::weakly_canonical(std::string(child));
        auto it_p = fp.begin(), it_c = fc.begin();
        while (it_p != fp.end() && it_c != fc.end()) {
            if (*it_p != *it_c) return false;
            ++it_p; ++it_c;
        }
        return it_p == fp.end();  // parent must be a prefix
    } catch (...) {
        return false;
    }
}

}  // namespace pi::core::path
