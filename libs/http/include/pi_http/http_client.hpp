// libs/http/include/pi_http/http_client.hpp
#pragma once

#include "pi_core/error.hpp"
#include "pi_core/result.hpp"
#include "http_types.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace pi::http {

/// Callback-based streaming consumer.
/// Return `false` to abort the request.
using ChunkCallback = std::function<bool(const HttpChunk&)>;

/// Optional proxy settings from environment variables.
struct ProxyConfig {
    std::string http_proxy;     // http://... or empty
    std::string https_proxy;    // https://... or empty
    std::string no_proxy;       // comma-separated hosts/CIDRs to skip
};

class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    /// Perform a one-shot request and return the full response.
    pi::core::Result<HttpResponse> send(const HttpRequest& req);

    /// Perform a streaming request. The callback is invoked with each chunk.
    /// Returns the final aggregated HttpResponse (headers + status), with body empty.
    /// If the callback returns false, the request is aborted.
    pi::core::Result<HttpResponse> send_streaming(const HttpRequest& req,
                                                   ChunkCallback cb);

    /// Abort any in-flight requests (closes their sockets / TLS sessions).
    void abort();

    /// Set HTTP proxy configuration explicitly.
    void set_proxy(const ProxyConfig& p) {
        std::lock_guard<std::mutex> g(mu_);
        proxy_ = p;
    }

    /// Get the current proxy configuration (env + explicit).
    ProxyConfig proxy_config() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    mutable std::mutex mu_;
    ProxyConfig proxy_;
    std::atomic<bool> aborted_{false};
};

/// Singleton accessor — pi only needs one HttpClient.
HttpClient& shared_client();

}  // namespace pi::http
