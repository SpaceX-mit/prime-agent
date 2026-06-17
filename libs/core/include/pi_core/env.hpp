// pi_core/env.hpp - Environment variable access
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace pi::core::env {

/// Get an environment variable. Empty string is treated as "missing".
std::optional<std::string> get(std::string_view name);

/// Get an environment variable or a default value.
std::string get_or(std::string_view name, std::string_view default_value);

/// Set an environment variable (process-local).
bool set(std::string_view name, std::string_view value);

/// Unset an environment variable.
bool unset(std::string_view name);

}  // namespace pi::core::env
