// libs/core/src/env.cpp
#include "pi_core/env.hpp"

#include <cstdlib>
#include <string>

namespace pi::core::env {

std::optional<std::string> get(std::string_view name) {
    if (name.empty()) return std::nullopt;
    std::string n{name};
    const char* v = std::getenv(n.c_str());
    if (!v) return std::nullopt;
    std::string out(v);
    if (out.empty()) return std::nullopt;
    return out;
}

std::string get_or(std::string_view name, std::string_view default_value) {
    auto v = get(name);
    return v ? *v : std::string(default_value);
}

bool set(std::string_view name, std::string_view value) {
    if (name.empty()) return false;
    return setenv(std::string(name).c_str(), std::string(value).c_str(), 1) == 0;
}

bool unset(std::string_view name) {
    if (name.empty()) return false;
    return unsetenv(std::string(name).c_str()) == 0;
}

}  // namespace pi::core::env
