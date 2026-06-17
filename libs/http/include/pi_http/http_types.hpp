// libs/http/include/pi_http/http_types.hpp
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace pi::http {

using Headers = std::map<std::string, std::string, std::less<>>;
using QueryParams = std::map<std::string, std::string, std::less<>>;

struct HttpRequest {
    std::string method = "GET";
    std::string url;
    Headers headers;
    QueryParams query;
    std::string body;            // empty if no body
    bool verify_tls = true;
    int timeout_ms = 60'000;
    int connect_timeout_ms = 10'000;

    /// Build the full URL with query string appended.
    std::string full_url() const;
};

struct HttpResponse {
    int status = 0;              // 0 means no response received
    std::string status_text;
    Headers headers;
    std::string body;
    std::string error_message;   // non-empty if transport-level failure

    bool ok() const noexcept { return status >= 200 && status < 300; }
};

/// A chunk emitted during streaming.
struct HttpChunk {
    std::string data;            // raw bytes (may be empty for heartbeats)
    bool is_final = false;
    int status = 0;
};

}  // namespace pi::http
