// libs/coding/include/pi_coding/oauth.hpp
// OAuth 2.0 PKCE flow primitives.
//
// Generic enough to support any provider that uses OAuth 2.0 with PKCE.
// The provider-specific URLs / client_id / token endpoint live in
// `OAuthConfig` (provided by the caller).
//
// Flow:
//   1. generate_pkce() -> verifier, challenge
//   2. start_callback_server(port) -> starts a tiny HTTP server on `port` that
//      captures the `?code=...` from the redirect, then shuts down.
//   3. authorization_url(...)  -> URL to open in the browser
//   4. exchange_code(config, code, verifier) -> access_token, refresh_token

#pragma once

#include "pi_core/error.hpp"
#include "pi_core/json.hpp"
#include "pi_core/result.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace pi::coding {

/// A PKCE verifier/challenge pair.
struct PKCE {
    std::string verifier;     // 43-128 chars, [A-Z a-z 0-9 - . _ ~]
    std::string challenge;    // base64url(SHA256(verifier))
};

/// Generate a PKCE verifier + S256 challenge.
PKCE generate_pkce();

/// Configuration for an OAuth provider's endpoints.
struct OAuthConfig {
    std::string client_id;
    std::string authorization_endpoint;
    std::string token_endpoint;
    std::string redirect_host = "127.0.0.1";
    uint16_t redirect_port = 0;       // 0 = pick a free port
    std::vector<std::string> scopes;
    /// Extra query params to add to the authorization URL.
    std::map<std::string, std::string> extra_auth_params;
};

/// Result of a successful token exchange.
struct OAuthCredentials {
    std::string access_token;
    std::string refresh_token;
    int64_t expires_at_ms = 0;       // 0 if unknown
    std::string account_id;          // provider-specific
    pi::core::Json extra = pi::core::Json::object();
};

/// Start a localhost HTTP server on the given port (0 = auto-pick).
/// Returns the actual port bound. The server captures a single `?code=`
/// request and shuts down.
class CallbackServer {
public:
    explicit CallbackServer(uint16_t port = 0);
    ~CallbackServer();

    CallbackServer(const CallbackServer&) = delete;
    CallbackServer& operator=(const CallbackServer&) = delete;

    /// The bound port (after construction).
    uint16_t port() const;

    /// Block up to `timeout_ms` waiting for the auth code.
    /// Returns the code on success.
    std::optional<std::string> wait_for_code(int timeout_ms);

    /// Stop the server (returns early from wait_for_code).
    void stop();

private:
    int fd_ = -1;
    uint16_t port_ = 0;
    std::optional<std::string> code_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
};

/// Build the authorization URL the user should visit.
std::string authorization_url(const OAuthConfig& cfg, const PKCE& pkce,
                              const std::string& state);

/// Exchange the auth code + verifier for access/refresh tokens.
pi::core::Result<OAuthCredentials> exchange_code(
    const OAuthConfig& cfg, const std::string& code, const PKCE& pkce);

/// Refresh an access token using the refresh_token grant.
pi::core::Result<OAuthCredentials> refresh_token(
    const OAuthConfig& cfg, const std::string& refresh_token);

}  // namespace pi::coding
