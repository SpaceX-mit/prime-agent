// libs/coding/src/auth_storage.cpp
#include "pi_coding/auth_storage.hpp"

#include "pi_core/file_io.hpp"
#include "pi_core/lockfile.hpp"

namespace pi::coding {

namespace {

pi::core::Json serialize(const AuthCredential& c) {
    pi::core::Json j;
    if (c.type == AuthCredential::Type::ApiKey) {
        j["type"] = "api_key";
        j["key"] = c.api_key.key;
        if (c.api_key.comment) j["comment"] = *c.api_key.comment;
    } else {
        j["type"] = "oauth";
        j["accessToken"] = c.oauth.access_token;
        j["refreshToken"] = c.oauth.refresh_token;
        j["expiresAt"] = c.oauth.expires_at_ms;
        if (c.oauth.account_id) j["accountId"] = *c.oauth.account_id;
        if (!c.oauth.extra.empty()) j["extra"] = c.oauth.extra;
    }
    return j;
}

AuthCredential deserialize(const pi::core::Json& j) {
    AuthCredential c;
    std::string t = j.value("type", std::string{"api_key"});
    if (t == "oauth") {
        c.type = AuthCredential::Type::OAuth;
        c.oauth.access_token = j.value("accessToken", std::string{});
        c.oauth.refresh_token = j.value("refreshToken", std::string{});
        c.oauth.expires_at_ms = j.value("expiresAt", int64_t{0});
        if (j.contains("accountId")) c.oauth.account_id = j["accountId"].get<std::string>();
        if (j.contains("extra")) c.oauth.extra = j["extra"];
    } else {
        c.type = AuthCredential::Type::ApiKey;
        c.api_key.key = j.value("key", std::string{});
        if (j.contains("comment")) c.api_key.comment = j["comment"].get<std::string>();
    }
    return c;
}

}  // namespace

AuthStorage::AuthStorage(std::string path) : path_(std::move(path)) {
    // Ensure parent dir exists.
    auto p = path_;
    auto slash = p.find_last_of('/');
    if (slash != std::string::npos) {
        pi::core::file::create_directories(p.substr(0, slash));
    }
}

std::map<std::string, AuthCredential> AuthStorage::read_all() const {
    std::map<std::string, AuthCredential> out;
    auto r = pi::core::file::read(path_);
    if (!r) return out;
    auto j = pi::core::tryParse(r.value());
    if (!j || !j->is_object()) return out;
    for (auto it = j->begin(); it != j->end(); ++it) {
        out.emplace(it.key(), deserialize(it.value()));
    }
    return out;
}

std::optional<AuthCredential> AuthStorage::get(const std::string& provider) const {
    auto all = read_all();
    auto it = all.find(provider);
    if (it == all.end()) return std::nullopt;
    return it->second;
}

void AuthStorage::set(const std::string& provider, const AuthCredential& cred) {
    pi::core::lockfile::FileLock lk(path_);
    auto guard = lk.acquire(std::chrono::seconds(5));
    if (!guard) return;
    auto all = read_all();
    all[provider] = cred;
    pi::core::Json j = pi::core::Json::object();
    for (auto& [k, v] : all) j[k] = serialize(v);
    pi::core::file::write_atomic(path_, j.dump(2) + "\n");
}

void AuthStorage::remove(const std::string& provider) {
    pi::core::lockfile::FileLock lk(path_);
    auto guard = lk.acquire(std::chrono::seconds(5));
    if (!guard) return;
    auto all = read_all();
    auto it = all.find(provider);
    if (it == all.end()) return;
    all.erase(it);
    pi::core::Json j = pi::core::Json::object();
    for (auto& [k, v] : all) j[k] = serialize(v);
    pi::core::file::write_atomic(path_, j.dump(2) + "\n");
}

std::vector<std::string> AuthStorage::list() const {
    auto all = read_all();
    std::vector<std::string> out;
    out.reserve(all.size());
    for (auto& [k, _] : all) out.push_back(k);
    return out;
}

}  // namespace pi::coding
