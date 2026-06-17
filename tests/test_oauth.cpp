// tests/test_oauth.cpp
#define TEST_FRAMEWORK_IMPLEMENT
#include "test_framework.hpp"

#include "pi_coding/oauth.hpp"
#include "pi_core/strutil.hpp"

#include <regex>
#include <string>

using namespace pi;
using namespace pi::coding;

TEST_CASE("PKCE generation produces non-empty verifier+challenge") {
    auto pkce = generate_pkce();
    CHECK(pkce.verifier.size() >= 43);
    CHECK(pkce.challenge.size() >= 40);  // 32 bytes -> 43 base64url chars
    // Verifier must only contain RFC 7636 unreserved characters.
    auto ok = std::regex_match(pkce.verifier,
        std::regex("^[A-Za-z0-9\\-_\\.~]+$"));
    CHECK(ok);
}

TEST_CASE("PKCE challenges differ across calls") {
    auto a = generate_pkce();
    auto b = generate_pkce();
    CHECK(a.verifier != b.verifier);
    CHECK(a.challenge != b.challenge);
}

TEST_CASE("authorization_url contains required PKCE params") {
    OAuthConfig cfg;
    cfg.client_id = "test-client";
    cfg.authorization_endpoint = "https://example.com/oauth/authorize";
    cfg.redirect_host = "127.0.0.1";
    cfg.redirect_port = 12345;
    cfg.scopes = {"read", "write"};

    PKCE pkce;
    pkce.verifier = "abc123";
    pkce.challenge = "xyz789";

    std::string url = authorization_url(cfg, pkce, "state-token");
    CHECK(url.find("response_type=code") != std::string::npos);
    CHECK(url.find("client_id=test-client") != std::string::npos);
    CHECK(url.find("code_challenge=xyz789") != std::string::npos);
    CHECK(url.find("code_challenge_method=S256") != std::string::npos);
    CHECK(url.find("state=state-token") != std::string::npos);
    CHECK(url.find("redirect_uri=") != std::string::npos);
    CHECK(url.find("scope=read%20write") != std::string::npos);
}

TEST_CASE("CallbackServer picks a free port and shuts down cleanly") {
    CallbackServer s(0);
    CHECK(s.port() != 0);
    s.stop();
    // Just verify we can construct and destruct without crashing.
}
