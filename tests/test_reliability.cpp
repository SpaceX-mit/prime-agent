// tests/test_reliability.cpp
// P1 reliability: error classification (retryable vs fatal) and
// human-readable error message mapping used by interactive + print mode.
#define TEST_FRAMEWORK_IMPLEMENT
#include "test_framework.hpp"

#include "pi_ai/stream_simple.hpp"

using namespace pi::ai;

TEST_CASE("retryable: rate limit and server errors") {
    CHECK(is_retryable_stream_error("anthropic: HTTP 429 Too Many Requests: {...}"));
    CHECK(is_retryable_stream_error("openai: HTTP 500 Internal Server Error"));
    CHECK(is_retryable_stream_error("google: HTTP 502 Bad Gateway"));
    CHECK(is_retryable_stream_error("x: HTTP 503 Service Unavailable"));
    CHECK(is_retryable_stream_error("x: HTTP 504 Gateway Timeout"));
    CHECK(is_retryable_stream_error("x: HTTP 408 Request Timeout"));
}

TEST_CASE("retryable: transport-level failures") {
    CHECK(is_retryable_stream_error("anthropic: request timed out after 60s"));
    CHECK(is_retryable_stream_error("minimax: connection refused"));
    CHECK(is_retryable_stream_error("openai: connect: network unreachable"));
    CHECK(is_retryable_stream_error("anthropic: stream ended unexpectedly"));
}

TEST_CASE("not retryable: client errors") {
    CHECK_FALSE(is_retryable_stream_error("anthropic: HTTP 401 Unauthorized"));
    CHECK_FALSE(is_retryable_stream_error("anthropic: HTTP 403 Forbidden"));
    CHECK_FALSE(is_retryable_stream_error("openai: HTTP 400 Bad Request"));
    CHECK_FALSE(is_retryable_stream_error("google: HTTP 404 Not Found"));
}

TEST_CASE("humanize: known statuses produce actionable text") {
    auto h429 = humanize_stream_error("anthropic", "anthropic: HTTP 429 ...");
    CHECK(h429.find("rate limited") != std::string::npos);
    CHECK(h429.find("anthropic") != std::string::npos);

    auto h401 = humanize_stream_error("openai", "openai: HTTP 401 Unauthorized");
    CHECK(h401.find("authentication failed") != std::string::npos);
    CHECK(h401.find("/login") != std::string::npos);

    auto h500 = humanize_stream_error("google", "google: HTTP 500 boom");
    CHECK(h500.find("server error") != std::string::npos);

    auto hto = humanize_stream_error("anthropic", "anthropic: request timed out");
    CHECK(hto.find("timed out") != std::string::npos);
}

TEST_CASE("humanize: unknown error is trimmed, never raw-dumped") {
    std::string huge(1000, 'x');
    auto h = humanize_stream_error("p", "weird: " + huge);
    CHECK(h.size() <= 201);  // 197 + ellipsis
}
