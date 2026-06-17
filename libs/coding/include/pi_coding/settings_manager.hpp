// libs/coding/include/pi_coding/settings_manager.hpp
#pragma once

#include "pi_core/json.hpp"

#include <functional>
#include <optional>
#include <string>

namespace pi::coding {

struct Settings {
    pi::core::Json data = pi::core::Json::object();
};

class SettingsManager {
public:
    /// Construct from global (~/.pi/agent/settings.json) and optional project (.pi/settings.json).
    SettingsManager(std::string global_path, std::string project_path = "");

    /// Re-read from disk.
    void reload();

    /// Read merged settings.
    const Settings& get() const { return merged_; }

    /// Patch and write to project (if project_path is set) or global.
    void patch(pi::core::Json patch);

    /// Add an error from a previous read attempt (for diagnostics).
    void record_error(const std::string& scope, const std::string& msg);

    /// Drain accumulated errors.
    std::vector<std::pair<std::string, std::string>> drain_errors();

    const std::string& global_path() const { return global_path_; }
    const std::string& project_path() const { return project_path_; }

private:
    std::string global_path_;
    std::string project_path_;
    Settings global_;
    Settings project_;
    Settings merged_;
    std::vector<std::pair<std::string, std::string>> errors_;

    void load_one(const std::string& path, Settings& out, const std::string& scope);
    Settings merge(const Settings& a, const Settings& b) const;
};

}  // namespace pi::coding
