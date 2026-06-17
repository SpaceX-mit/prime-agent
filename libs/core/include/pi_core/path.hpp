// pi_core/path.hpp - Path operations
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace pi::core::path {

/// Expand a leading "~" or "~/" to the user's home directory.
/// "~user" is not supported.
std::string expand_home(std::string_view p);

/// Join two path components with the platform separator.
std::string join(std::string_view a, std::string_view b);

/// Returns the path normalized: collapse "..", ".", duplicate separators,
/// and resolve symbolic links if `resolve_symlinks` is true.
std::string normalize(std::string_view p, bool resolve_symlinks = false);

/// True if `child` is the same as or inside `parent`.
bool is_within(std::string_view parent, std::string_view child);

/// Common locations.
std::optional<std::string> home_dir();
std::optional<std::string> xdg_config_home();    // ~/.config by default
std::optional<std::string> xdg_data_home();      // ~/.local/share by default
std::optional<std::string> xdg_cache_home();     // ~/.cache by default
std::optional<std::string> current_working_dir();

}  // namespace pi::core::path
