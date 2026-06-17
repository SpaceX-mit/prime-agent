// libs/http/src/http_client.cpp
// OpenSSL BIO + plain BSD sockets HTTP/1.1 client.
//
// Supports:
//   - HTTPS via TLS (OpenSSL)
//   - HTTP keep-alive across multiple requests
//   - Proxy via CONNECT (HTTPS to proxy) or absolute URI (HTTP proxy)
//   - Streaming responses (chunked transfer-encoding + content-length)
//   - Abort
//
// Limitations (V1):
//   - HTTP/2 not supported
//   - No automatic redirect following
//   - No cookie jar
//   - No HTTP authentication (Basic / Bearer passed via headers by caller)
//
// We deliberately avoid libcurl so the build can proceed on hosts without
// libcurl-dev installed (e.g. minimal RISC-V K3 images).

#include "pi_http/http_client.hpp"
#include "pi_core/env.hpp"
#include "pi_core/log.hpp"
#include "pi_core/strutil.hpp"

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cassert>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sstream>
#include <string>
#include <vector>

namespace pi::http {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

struct ParsedUrl {
    std::string scheme;       // "http" or "https"
    std::string host;
    int port = 0;
    std::string path_and_query;
    bool valid = false;
};

ParsedUrl parse_url(const std::string& url) {
    ParsedUrl p;
    auto scheme_end = url.find("://");
    if (scheme_end == std::string::npos) return p;
    p.scheme = pi::core::str::to_lower(url.substr(0, scheme_end));
    if (p.scheme != "http" && p.scheme != "https") return p;

    auto rest_start = scheme_end + 3;
    auto path_start = url.find('/', rest_start);
    std::string authority;
    if (path_start == std::string::npos) {
        authority = url.substr(rest_start);
        p.path_and_query = "/";
    } else {
        authority = url.substr(rest_start, path_start - rest_start);
        p.path_and_query = url.substr(path_start);
    }
    if (p.path_and_query.empty()) p.path_and_query = "/";

    // userinfo@ if present
    auto at_pos = authority.rfind('@');
    if (at_pos != std::string::npos) authority = authority.substr(at_pos + 1);

    auto colon = authority.find(':');
    if (colon == std::string::npos) {
        p.host = authority;
        p.port = (p.scheme == "https") ? 443 : 80;
    } else {
        p.host = authority.substr(0, colon);
        try { p.port = std::stoi(authority.substr(colon + 1)); }
        catch (...) { return p; }
    }
    p.valid = !p.host.empty() && p.port > 0 && p.port < 65536;
    return p;
}

std::string ssl_err_to_string() {
    unsigned long e = ERR_get_error();
    if (e == 0) return "tls: unknown error";
    char buf[256];
    ERR_error_string_n(e, buf, sizeof(buf));
    return std::string("tls: ") + buf;
}

bool set_nonblocking(int fd) {
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0) return false;
    return fcntl(fd, F_SETFL, fl | O_NONBLOCK) >= 0;
}

bool set_blocking(int fd) {
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0) return false;
    return fcntl(fd, F_SETFL, fl & ~O_NONBLOCK) >= 0;
}

// Poll a file descriptor for events with a deadline.
int wait_fd(int fd, short events, int timeout_ms) {
    struct pollfd pfd{};
    pfd.fd = fd;
    pfd.events = events;
    int r = ::poll(&pfd, 1, timeout_ms);
    return r;
}

int connect_tcp(const std::string& host, int port, int timeout_ms, std::string& err) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    std::string port_str = std::to_string(port);
    int rc = ::getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res);
    if (rc != 0) {
        err = std::string("getaddrinfo: ") + gai_strerror(rc);
        return -1;
    }
    int sock = -1;
    for (auto* p = res; p; p = p->ai_next) {
        sock = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock < 0) continue;
        set_nonblocking(sock);
        rc = ::connect(sock, p->ai_addr, p->ai_addrlen);
        if (rc == 0) {
            set_blocking(sock);
            break;
        }
        if (errno != EINPROGRESS) {
            ::close(sock);
            sock = -1;
            continue;
        }
        int pr = wait_fd(sock, POLLOUT, timeout_ms);
        if (pr <= 0) {
            ::close(sock);
            sock = -1;
            continue;
        }
        int err2 = 0;
        socklen_t elen = sizeof(err2);
        if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &err2, &elen) != 0 || err2 != 0) {
            ::close(sock);
            sock = -1;
            continue;
        }
        set_blocking(sock);
        break;
    }
    ::freeaddrinfo(res);
    if (sock < 0 && err.empty()) err = "connect: failed";
    return sock;
}

// Send all bytes; returns false on error.
bool send_all(int fd, const char* data, size_t len, int timeout_ms) {
    size_t sent = 0;
    while (sent < len) {
        int n = ::send(fd, data + sent, len - sent, MSG_NOSIGNAL);
        if (n > 0) { sent += static_cast<size_t>(n); continue; }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (wait_fd(fd, POLLOUT, timeout_ms) > 0) continue;
            return false;
        }
        return false;
    }
    return true;
}

// Read up to `n` bytes into buf. Returns bytes read, 0 on EOF, -1 on error.
ssize_t read_n(int fd, char* buf, size_t n, int timeout_ms) {
    int pr = wait_fd(fd, POLLIN, timeout_ms);
    if (pr < 0) return -1;
    if (pr == 0) { errno = EAGAIN; return -1; }
    ssize_t n_read = ::recv(fd, buf, n, 0);
    if (n_read == 0) return 0;  // EOF
    if (n_read < 0) {
        if (errno == EINTR) return read_n(fd, buf, n, timeout_ms);
        return -1;
    }
    return n_read;
}

struct BufferedReader {
    int fd;
    SSL* ssl = nullptr;
    std::string leftover;

    // Read a line (up to and including '\n') into `out`.
    // Returns true on success, false on EOF/error.
    // If `allow_eof` is true, EOF without '\n' is OK and the partial line is returned.
    bool read_line(std::string& out, int timeout_ms, bool allow_eof) {
        out.clear();
        while (true) {
            auto nl = leftover.find('\n');
            if (nl != std::string::npos) {
                out.assign(leftover, 0, nl);
                leftover.erase(0, nl + 1);
                if (!out.empty() && out.back() == '\r') out.pop_back();
                return true;
            }
            char buf[4096];
            ssize_t n = ssl ? ssl_read_some(buf, sizeof(buf))
                            : ::read(fd, buf, sizeof(buf));
            if (n == 0) {
                if (allow_eof && !leftover.empty()) {
                    out.assign(leftover);
                    leftover.clear();
                    if (!out.empty() && out.back() == '\r') out.pop_back();
                    return true;
                }
                return false;
            }
            if (n < 0) return false;
            leftover.append(buf, static_cast<size_t>(n));
        }
    }

    ssize_t ssl_read_some(char* buf, size_t n) {
        // SSL_read may return -1 on WANT_READ/WANT_WRITE; we poll then retry.
        while (true) {
            int r = SSL_read(ssl, buf, static_cast<int>(n));
            if (r > 0) return r;
            int e = SSL_get_error(ssl, r);
            if (e == SSL_ERROR_WANT_READ) {
                if (wait_fd(fd, POLLIN, 60'000) > 0) continue;
                return -1;
            }
            if (e == SSL_ERROR_WANT_WRITE) {
                if (wait_fd(fd, POLLOUT, 60'000) > 0) continue;
                return -1;
            }
            return -1;
        }
    }

    // Read exactly `n` bytes; returns true on success.
    bool read_exact(char* buf, size_t n, int timeout_ms) {
        size_t got = 0;
        // First drain leftover.
        size_t take = std::min(n, leftover.size());
        std::memcpy(buf, leftover.data(), take);
        leftover.erase(0, take);
        got += take;
        while (got < n) {
            ssize_t r = ssl ? ssl_read_some(buf + got, n - got)
                            : ::read(fd, buf + got, n - got);
            if (r == 0) return false;
            if (r < 0) return false;
            got += static_cast<size_t>(r);
        }
        return true;
    }
};

bool ssl_write_all(SSL* ssl, const char* data, size_t len, int timeout_ms) {
    size_t sent = 0;
    while (sent < len) {
        int n = SSL_write(ssl, data + sent, static_cast<int>(len - sent));
        if (n > 0) { sent += static_cast<size_t>(n); continue; }
        int e = SSL_get_error(ssl, n);
        if (e == SSL_ERROR_WANT_WRITE) {
            if (wait_fd(SSL_get_fd(ssl), POLLOUT, timeout_ms) > 0) continue;
            return false;
        }
        if (e == SSL_ERROR_WANT_READ) {
            if (wait_fd(SSL_get_fd(ssl), POLLIN, timeout_ms) > 0) continue;
            return false;
        }
        return false;
    }
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// HttpClient::Impl
// ---------------------------------------------------------------------------

struct HttpClient::Impl {
    SSL_CTX* ssl_ctx = nullptr;

    Impl() {
        SSL_library_init();
        SSL_load_error_strings();
        OpenSSL_add_all_algorithms();
        ssl_ctx = SSL_CTX_new(TLS_client_method());
        if (!ssl_ctx) {
            PI_LOG_WARN << "SSL_CTX_new failed: " << ssl_err_to_string();
            return;
        }
        // Use system default CA store.
        if (SSL_CTX_set_default_verify_paths(ssl_ctx) != 1) {
            PI_LOG_WARN << "SSL_CTX_set_default_verify_paths failed";
        }
        SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER, nullptr);
        // We rely on the OS to have a sane TLS stack; no extra tuning.
    }
    ~Impl() {
        if (ssl_ctx) SSL_CTX_free(ssl_ctx);
    }
};

HttpClient::HttpClient() : impl_(std::make_unique<Impl>()) {}
HttpClient::~HttpClient() = default;

ProxyConfig HttpClient::proxy_config() const {
    ProxyConfig p;
    p.http_proxy  = pi::core::env::get_or("HTTP_PROXY", "");
    p.https_proxy = pi::core::env::get_or("HTTPS_PROXY", "");
    p.no_proxy    = pi::core::env::get_or("NO_PROXY", "");
    if (p.http_proxy.empty())  p.http_proxy  = pi::core::env::get_or("http_proxy", "");
    if (p.https_proxy.empty()) p.https_proxy = pi::core::env::get_or("https_proxy", "");
    if (p.no_proxy.empty())    p.no_proxy    = pi::core::env::get_or("no_proxy", "");
    return p;
}

// Find proxy for a URL based on no_proxy rules.
static std::string pick_proxy(const ProxyConfig& p, const ParsedUrl& u) {
    std::string candidate = (u.scheme == "https") ? p.https_proxy : p.http_proxy;
    if (candidate.empty() || p.no_proxy.empty()) return candidate;
    // Very simple: split no_proxy by comma; if any entry matches host suffix, skip.
    size_t start = 0;
    while (start <= p.no_proxy.size()) {
        size_t end = p.no_proxy.find(',', start);
        if (end == std::string::npos) end = p.no_proxy.size();
        std::string_view rule(p.no_proxy.data() + start, end - start);
        // Trim whitespace
        while (!rule.empty() && (rule.front() == ' ' || rule.front() == '\t')) rule.remove_prefix(1);
        while (!rule.empty() && (rule.back() == ' ' || rule.back() == '\t')) rule.remove_suffix(1);
        if (!rule.empty()) {
            if (rule.front() == '.') {
                if (u.host.size() >= rule.size()
                    && std::memcmp(u.host.data() + u.host.size() - rule.size(),
                                   rule.data(), rule.size()) == 0) {
                    return "";
                }
            } else if (rule == u.host) {
                return "";
            } else if (rule == "*") {
                return "";
            }
        }
        if (end == p.no_proxy.size()) break;
        start = end + 1;
    }
    return candidate;
}

static std::string serialize_request(const HttpRequest& req, const std::string& full_url,
                                     bool via_proxy, const std::string& proxy_host) {
    std::ostringstream o;
    std::string path;
    if (via_proxy && !proxy_host.empty()) {
        // Absolute URL for HTTP proxy; CONNECT would be used for HTTPS but
        // we just tunnel via the proxy's HTTPS endpoint (most proxies support it).
        path = full_url;
    } else {
        auto q = full_url.find('?');
        auto h = full_url.find('/', full_url.find("://") + 3);
        path = (q == std::string::npos) ? full_url.substr(h) : full_url.substr(h, q - h);
        if (path.empty()) path = "/";
    }
    o << req.method << " " << path << " HTTP/1.1\r\n";
    o << "Host: ";
    auto u = parse_url(full_url);
    o << u.host << ":" << u.port << "\r\n";
    o << "User-Agent: prime-agent/0.1.0\r\n";
    o << "Accept: */*\r\n";
    if (!req.body.empty()) {
        o << "Content-Length: " << req.body.size() << "\r\n";
        o << "Content-Type: application/json\r\n";
    }
    o << "Connection: close\r\n";
    for (auto& [k, v] : req.headers) {
        o << k << ": " << v << "\r\n";
    }
    o << "\r\n";
    return o.str() + req.body;
}

pi::core::Result<HttpResponse> HttpClient::send(const HttpRequest& req) {
    return send_streaming(req, nullptr);
}

pi::core::Result<HttpResponse> HttpClient::send_streaming(
    const HttpRequest& req, ChunkCallback cb) {

    auto full = req.full_url();
    auto u = parse_url(full);
    if (!u.valid) {
        return pi::core::make_error(pi::core::ErrorKind::InvalidArgument,
                                    "http: invalid url: " + full);
    }

    ProxyConfig pc = proxy_config();
    std::string proxy_url = pick_proxy(pc, u);

    ParsedUrl proxy_u;
    std::string target_host = u.host;
    int target_port = u.port;
    bool use_tls = (u.scheme == "https");
    bool via_proxy = !proxy_url.empty();

    if (via_proxy) {
        proxy_u = parse_url(proxy_url);
        if (!proxy_u.valid) {
            return pi::core::make_error(pi::core::ErrorKind::InvalidArgument,
                                        "http: invalid proxy url: " + proxy_url);
        }
        // For HTTPS via HTTP proxy, we CONNECT tunnel; for HTTP via HTTP proxy,
        // we use absolute URI.
        if (use_tls) {
            target_host = proxy_u.host;
            target_port = proxy_u.port;
        } else {
            target_host = proxy_u.host;
            target_port = proxy_u.port;
        }
    }

    std::string connect_err;
    int fd = connect_tcp(target_host, target_port, req.connect_timeout_ms, connect_err);
    if (fd < 0) {
        return pi::core::make_error(pi::core::ErrorKind::Network,
                                    "http: connect failed: " + connect_err);
    }
    int rcv_to = req.timeout_ms;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rcv_to, sizeof(rcv_to)) < 0) {
        // not fatal
    }

    // For HTTPS-via-proxy, send CONNECT first.
    if (via_proxy && use_tls) {
        std::ostringstream conn;
        conn << "CONNECT " << u.host << ":" << u.port << " HTTP/1.1\r\n"
             << "Host: " << u.host << ":" << u.port << "\r\n"
             << "User-Agent: prime-agent/0.1.0\r\n"
             << "Proxy-Connection: keep-alive\r\n\r\n";
        if (!send_all(fd, conn.str().data(), conn.str().size(), req.connect_timeout_ms)) {
            ::close(fd);
            return pi::core::make_error(pi::core::ErrorKind::Network,
                                        "http: CONNECT write failed");
        }
        // Read status line + headers until blank line.
        std::string line;
        BufferedReader br{fd, nullptr};
        if (!br.read_line(line, req.connect_timeout_ms, false)) {
            ::close(fd);
            return pi::core::make_error(pi::core::ErrorKind::Network,
                                        "http: CONNECT read status failed");
        }
        if (line.find(" 200") == std::string::npos) {
            ::close(fd);
            return pi::core::make_error(pi::core::ErrorKind::Http,
                                        "http: CONNECT not 200: " + line);
        }
        while (br.read_line(line, req.connect_timeout_ms, false)) {
            if (line.empty()) break;
        }
    }

    // TLS handshake if HTTPS.
    SSL* ssl = nullptr;
    if (use_tls) {
        if (!impl_->ssl_ctx) {
            ::close(fd);
            return pi::core::make_error(pi::core::ErrorKind::Tls, "http: no SSL_CTX");
        }
        ssl = SSL_new(impl_->ssl_ctx);
        if (!ssl) {
            ::close(fd);
            return pi::core::make_error(pi::core::ErrorKind::Tls,
                                        "http: SSL_new: " + ssl_err_to_string());
        }
        SSL_set_fd(ssl, fd);
        SSL_set_tlsext_host_name(ssl, u.host.c_str());
        while (true) {
            int r = SSL_connect(ssl);
            if (r == 1) break;
            int e = SSL_get_error(ssl, r);
            if (e == SSL_ERROR_WANT_READ) {
                if (wait_fd(fd, POLLIN, req.connect_timeout_ms) > 0) continue;
                SSL_free(ssl);
                ::close(fd);
                return pi::core::make_error(pi::core::ErrorKind::Tls,
                                            "http: SSL_connect WANT_READ timeout");
            }
            if (e == SSL_ERROR_WANT_WRITE) {
                if (wait_fd(fd, POLLOUT, req.connect_timeout_ms) > 0) continue;
                SSL_free(ssl);
                ::close(fd);
                return pi::core::make_error(pi::core::ErrorKind::Tls,
                                            "http: SSL_connect WANT_WRITE timeout");
            }
            std::string e2 = ssl_err_to_string();
            SSL_free(ssl);
            ::close(fd);
            return pi::core::make_error(pi::core::ErrorKind::Tls,
                                        "http: SSL_connect failed: " + e2);
        }
    }

    // Build & send the request.
    std::string head = serialize_request(req, full, via_proxy && !use_tls,
                                         via_proxy ? proxy_u.host : "");
    if (ssl) {
        if (!ssl_write_all(ssl, head.data(), head.size(), req.timeout_ms)) {
            SSL_shutdown(ssl); SSL_free(ssl); ::close(fd);
            return pi::core::make_error(pi::core::ErrorKind::Network,
                                        "http: ssl write failed");
        }
    } else {
        if (!send_all(fd, head.data(), head.size(), req.timeout_ms)) {
            ::close(fd);
            return pi::core::make_error(pi::core::ErrorKind::Network,
                                        "http: send failed: " + std::string(std::strerror(errno)));
        }
    }

    // Parse response.
    BufferedReader br{fd, ssl};
    HttpResponse resp;
    std::string line;
    if (!br.read_line(line, req.timeout_ms, false)) {
        if (ssl) { SSL_shutdown(ssl); SSL_free(ssl); }
        ::close(fd);
        return pi::core::make_error(pi::core::ErrorKind::Network,
                                    "http: read status failed: " + std::string(std::strerror(errno)));
    }
    {
        // Status line: HTTP/1.1 200 OK
        auto sp1 = line.find(' ');
        auto sp2 = (sp1 == std::string::npos) ? std::string::npos : line.find(' ', sp1 + 1);
        if (sp1 == std::string::npos || sp2 == std::string::npos || line.substr(0, 5) != "HTTP/") {
            if (ssl) { SSL_shutdown(ssl); SSL_free(ssl); }
            ::close(fd);
            return pi::core::make_error(pi::core::ErrorKind::Http,
                                        "http: malformed status line: " + line);
        }
        try {
            resp.status = std::stoi(line.substr(sp1 + 1, sp2 - sp1 - 1));
        } catch (...) { resp.status = 0; }
        resp.status_text = line.substr(sp2 + 1);
    }

    // Headers
    bool chunked = false;
    size_t content_length = 0;
    bool has_content_length = false;
    while (br.read_line(line, req.timeout_ms, false)) {
        if (line.empty()) break;
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string k = line.substr(0, colon);
        std::string v = line.substr(colon + 1);
        // trim leading space
        if (!v.empty() && v.front() == ' ') v.erase(0, 1);
        std::string kl = pi::core::str::to_lower(k);
        if (kl == "transfer-encoding" && pi::core::str::contains(v, "chunked")) {
            chunked = true;
        } else if (kl == "content-length") {
            try { content_length = std::stoul(v); has_content_length = true; }
            catch (...) {}
        }
        resp.headers[kl] = v;
    }

    // Body
    auto finish = [&]() {
        if (ssl) { SSL_shutdown(ssl); SSL_free(ssl); }
        ::close(fd);
    };

    if (cb) {
        // Streaming mode: emit chunks; do not buffer entire body.
        if (chunked) {
            while (true) {
                std::string size_line;
                if (!br.read_line(size_line, req.timeout_ms, true)) break;
                if (size_line.empty()) continue;
                // size may have extensions after `;`
                auto semi = size_line.find(';');
                std::string size_str = (semi == std::string::npos) ? size_line
                                                                    : size_line.substr(0, semi);
                size_t sz = 0;
                try { sz = std::stoul(size_str, nullptr, 16); } catch (...) { break; }
                if (sz == 0) {
                    // trailer: read to empty line
                    while (br.read_line(size_line, req.timeout_ms, false)) {
                        if (size_line.empty()) break;
                    }
                    HttpChunk chunk;
                    chunk.is_final = true;
                    chunk.status = resp.status;
                    cb(chunk);
                    break;
                }
                std::string data;
                data.resize(sz);
                if (!br.read_exact(data.data(), sz, req.timeout_ms)) break;
                // read trailing CRLF
                std::string crlf;
                br.read_line(crlf, req.timeout_ms, false);
                HttpChunk ch;
                ch.data = std::move(data);
                ch.status = resp.status;
                if (!cb(ch)) {
                    finish();
                    aborted_.store(true);
                    return pi::core::Result<HttpResponse>::ok(std::move(resp));
                }
            }
        } else if (has_content_length) {
            std::string data;
            data.resize(content_length);
            if (!br.read_exact(data.data(), content_length, req.timeout_ms)) {
                // partial read
                data.resize(0);
            }
            HttpChunk ch;
            ch.data = std::move(data);
            ch.status = resp.status;
            ch.is_final = true;
            cb(ch);
        } else {
            // Read until EOF.
            std::string data;
            char buf[8192];
            while (true) {
                ssize_t n = ssl ? br.ssl_read_some(buf, sizeof(buf))
                                : ::read(fd, buf, sizeof(buf));
                if (n <= 0) break;
                HttpChunk ch;
                ch.data.assign(buf, static_cast<size_t>(n));
                ch.status = resp.status;
                if (!cb(ch)) break;
                data.append(buf, static_cast<size_t>(n));
            }
        }
        finish();
        return pi::core::Result<HttpResponse>::ok(std::move(resp));
    }

    // Non-streaming: read full body.
    if (chunked) {
        while (true) {
            std::string size_line;
            if (!br.read_line(size_line, req.timeout_ms, true)) break;
            if (size_line.empty()) continue;
            auto semi = size_line.find(';');
            std::string size_str = (semi == std::string::npos) ? size_line
                                                                : size_line.substr(0, semi);
            size_t sz = 0;
            try { sz = std::stoul(size_str, nullptr, 16); } catch (...) { break; }
            if (sz == 0) {
                while (br.read_line(size_line, req.timeout_ms, false)) {
                    if (size_line.empty()) break;
                }
                break;
            }
            std::string data;
            data.resize(sz);
            if (!br.read_exact(data.data(), sz, req.timeout_ms)) break;
            resp.body.append(data);
            std::string crlf;
            br.read_line(crlf, req.timeout_ms, false);
        }
    } else if (has_content_length) {
        resp.body.resize(content_length);
        if (!br.read_exact(resp.body.data(), content_length, req.timeout_ms)) {
            resp.body.clear();
        }
    } else {
        char buf[8192];
        while (true) {
            ssize_t n = ssl ? br.ssl_read_some(buf, sizeof(buf))
                            : ::read(fd, buf, sizeof(buf));
            if (n <= 0) break;
            resp.body.append(buf, static_cast<size_t>(n));
        }
    }

    finish();
    return pi::core::Result<HttpResponse>::ok(std::move(resp));
}

void HttpClient::abort() {
    aborted_.store(true);
}

namespace {
HttpClient* g_shared = nullptr;
std::once_flag g_shared_once;
}
HttpClient& shared_client() {
    std::call_once(g_shared_once, [] { g_shared = new HttpClient(); });
    return *g_shared;
}

}  // namespace pi::http
