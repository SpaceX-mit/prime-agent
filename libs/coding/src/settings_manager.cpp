// libs/coding/src/settings_manager.cpp
#include "pi_coding/settings_manager.hpp"

#include "pi_core/file_io.hpp"

namespace pi::coding {

namespace {
void deep_merge(const pi::core::Json& src, pi::core::Json& dst) {
    if (!src.is_object()) return;
    for (auto it = src.begin(); it != src.end(); ++it) {
        if (dst.contains(it.key()) && dst[it.key()].is_object() && it.value().is_object()) {
            deep_merge(it.value(), dst[it.key()]);
        } else {
            dst[it.key()] = it.value();
        }
    }
}
}  // namespace

SettingsManager::SettingsManager(std::string global_path, std::string project_path)
    : global_path_(std::move(global_path)), project_path_(std::move(project_path)) {
    reload();
}

void SettingsManager::load_one(const std::string& path, Settings& out, const std::string& scope) {
    if (path.empty()) return;
    auto r = pi::core::file::read(path);
    if (!r) {
        if (r.error().kind != pi::core::ErrorKind::NotFound) {
            errors_.emplace_back(scope, r.error().to_string());
        }
        return;
    }
    auto j = pi::core::tryParse(r.value());
    if (!j || !j->is_object()) {
        errors_.emplace_back(scope, "invalid JSON");
        return;
    }
    out.data = *j;
}

void SettingsManager::reload() {
    global_ = Settings{};
    project_ = Settings{};
    errors_.clear();
    load_one(global_path_, global_, "global");
    load_one(project_path_, project_, "project");
    merged_ = merge(global_, project_);
}

Settings SettingsManager::merge(const Settings& a, const Settings& b) const {
    Settings out;
    out.data = a.data;  // copy
    deep_merge(b.data, out.data);
    return out;
}

void SettingsManager::patch(pi::core::Json patch) {
    // Write to project if set, otherwise global.
    std::string target = !project_path_.empty() ? project_path_ : global_path_;
    Settings current;
    load_one(target, current, !project_path_.empty() ? "project" : "global");
    deep_merge(patch, current.data);
    auto w = pi::core::file::write_atomic(target, current.data.dump(2) + "\n");
    if (w) {
        reload();
    } else {
        errors_.emplace_back("patch", w.error().to_string());
    }
}

void SettingsManager::record_error(const std::string& scope, const std::string& msg) {
    errors_.emplace_back(scope, msg);
}

std::vector<std::pair<std::string, std::string>> SettingsManager::drain_errors() {
    auto out = std::move(errors_);
    errors_.clear();
    return out;
}

}  // namespace pi::coding
