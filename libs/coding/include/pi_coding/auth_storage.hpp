// libs/coding/include/pi_coding/auth_storage.hpp
#pragma once

#include "pi_core/json.hpp"

#include <map>
#include <optional>
#include <string>

namespace pi::coding {

struct ApiKeyCredential {
    std::string key;
    std::optional<std::string> comment;
};

struct OAuthCredential {
    std::string access_token;
    std::string refresh_token;
    int64_t expires_at_ms = 0;
    std::optional<std::string> account_id;
    /// Provider-specific extras (e.g. anthropic: "user_email", openai: "account_id").
    pi::core::Json extra = pi::core::Json::object();
};

struct AuthCredential {
    enum class Type { ApiKey, OAuth } type = Type::ApiKey;
    ApiKeyCredential api_key;
    OAuthCredential oauth;
};

class AuthStorage {
public:
    /// Construct from a JSON file path. The file is created on first write.
    explicit AuthStorage(std::string path);

    /// Read all credentials.
    std::map<std::string, AuthCredential> read_all() const;

    /// Get the credential for a provider.
    std::optional<AuthCredential> get(const std::string& provider) const;

    /// Set a credential (acquires an exclusive lock).
    void set(const std::string& provider, const AuthCredential& cred);

    /// Remove a credential.
    void remove(const std::string& provider);

    /// List provider names that have credentials.
    std::vector<std::string> list() const;

    const std::string& path() const { return path_; }

private:
    std::string path_;
};

}  // namespace pi::coding
