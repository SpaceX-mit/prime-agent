// libs/coding/src/oauth.cpp
// OAuth 2.0 PKCE primitives.

#include "pi_coding/oauth.hpp"
#include "pi_core/strutil.hpp"
#include "pi_http/http_client.hpp"

#include <openssl/rand.h>
#include <openssl/sha.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>

namespace pi::coding {

namespace {

std::string base64url(const unsigned char* data, size_t len) {
    static const char* kAlpha =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    size_t i = 0;
    while (i + 3 <= len) {
        uint32_t v = (data[i] << 16) | (data[i+1] << 8) | data[i+2];
        out += kAlpha[(v >> 18) & 0x3f];
        out += kAlpha[(v >> 12) & 0x3f];
        out += kAlpha[(v >> 6) & 0x3f];
        out += kAlpha[v & 0x3f];
        i += 3;
    }
    if (i + 1 == len) {
        uint32_t v = data[i] << 16;
        out += kAlpha[(v >> 18) & 0x3f];
        out += kAlpha[(v >> 12) & 0x3f];
    } else if (i + 2 == len) {
        uint32_t v = (data[i] << 16) | (data[i+1] << 8);
        out += kAlpha[(v >> 18) & 0x3f];
        out += kAlpha[(v >> 12) & 0x3f];
        out += kAlpha[(v >> 6) & 0x3f];
    }
    return out;
}

std::string url_encode(const std::string& s) {
    std::ostringstream o;
    o.fill('0');
    o << std::hex;
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
            || (c >= '0' && c <= '9') || c == '-' || c == '_'
            || c == '.' || c == '~') {
            o << c;
        } else {
            o << '%' << std::uppercase;
            o.width(2);
            o << (int)c;
            o << std::nouppercase;
        }
    }
    return o.str();
}

std::string gen_random_base64url(size_t bytes) {
    std::vector<unsigned char> buf(bytes);
    if (RAND_bytes(buf.data(), static_cast<int>(bytes)) != 1) {
        // Fall back to time-seeded rand.
        static thread_local std::mt19937_64 rng{
            static_cast<uint64_t>(
                std::chrono::steady_clock::now().time_since_epoch().count())};
        for (auto& b : buf) b = static_cast<unsigned char>(rng() & 0xff);
    }
    return base64url(buf.data(), buf.size());
}

uint16_t pick_free_port() {
    int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return 0;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(sock);
        return 0;
    }
    socklen_t len = sizeof(addr);
    if (::getsockname(sock, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
        ::close(sock);
        return 0;
    }
    uint16_t port = ntohs(addr.sin_port);
    // Listen so the OS reserves the port.
    ::listen(sock, 1);
    return port;
}

}  // namespace

PKCE generate_pkce() {
    PKCE p;
    // RFC 7636: verifier is 43-128 chars from the unreserved set.
    // 32 random bytes -> ~43 chars base64url.
    p.verifier = gen_random_base64url(32);

    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(p.verifier.data()),
           p.verifier.size(), hash);
    p.challenge = base64url(hash, SHA256_DIGEST_LENGTH);
    return p;
}

std::string authorization_url(const OAuthConfig& cfg, const PKCE& pkce,
                              const std::string& state) {
    std::ostringstream o;
    o << cfg.authorization_endpoint;
    o << (cfg.authorization_endpoint.find('?') == std::string::npos ? '?' : '&');
    o << "response_type=code";
    o << "&client_id=" << url_encode(cfg.client_id);
    o << "&redirect_uri=" << url_encode(
        "http://" + cfg.redirect_host + ":" + std::to_string(cfg.redirect_port) + "/callback");
    o << "&code_challenge=" << url_encode(pkce.challenge);
    o << "&code_challenge_method=S256";
    o << "&state=" << url_encode(state);
    if (!cfg.scopes.empty()) {
        o << "&scope=";
        std::string joined;
        for (size_t i = 0; i < cfg.scopes.size(); ++i) {
            if (i) joined += " ";
            joined += cfg.scopes[i];
        }
        o << url_encode(joined);
    }
    for (auto& [k, v] : cfg.extra_auth_params) {
        o << "&" << url_encode(k) << "=" << url_encode(v);
    }
    return o.str();
}

CallbackServer::CallbackServer(uint16_t port) : port_(port) {
    if (port_ == 0) port_ = pick_free_port();
    if (port_ == 0) return;
    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) return;
    int yes = 1;
    ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port_);
    if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd_);
        fd_ = -1;
        return;
    }
    if (::listen(fd_, 1) != 0) {
        ::close(fd_);
        fd_ = -1;
        return;
    }
    // Discover actual port (in case kernel picked a different one).
    sockaddr_in actual{};
    socklen_t alen = sizeof(actual);
    if (::getsockname(fd_, reinterpret_cast<sockaddr*>(&actual), &alen) == 0) {
        port_ = ntohs(actual.sin_port);
    }

    thread_ = std::thread([this]() {
        while (!stop_.load()) {
            struct pollfd pfd{fd_, POLLIN, 0};
            int pr = ::poll(&pfd, 1, 200);
            if (pr <= 0) continue;
            sockaddr_in caddr{};
            socklen_t clen = sizeof(caddr);
            int cfd = ::accept(fd_, reinterpret_cast<sockaddr*>(&caddr), &clen);
            if (cfd < 0) continue;

            // Read the HTTP request (one short GET).
            std::string req;
            char buf[2048];
            while (true) {
                ssize_t n = ::read(cfd, buf, sizeof(buf));
                if (n <= 0) break;
                req.append(buf, static_cast<size_t>(n));
                if (req.find("\r\n\r\n") != std::string::npos) break;
                if (req.size() > 8192) break;
            }
            // Parse the request line.
            std::string path;
            if (req.compare(0, 4, "GET ") == 0) {
                auto sp = req.find(' ', 4);
                if (sp != std::string::npos) path = req.substr(4, sp - 4);
            }
            // Find ?code=...
            std::string code;
            auto q = path.find('?');
            if (q != std::string::npos) {
                std::string qs = path.substr(q + 1);
                size_t pos = 0;
                while (pos < qs.size()) {
                    auto amp = qs.find('&', pos);
                    if (amp == std::string::npos) amp = qs.size();
                    std::string kv = qs.substr(pos, amp - pos);
                    auto eq = kv.find('=');
                    if (eq != std::string::npos) {
                        std::string k = kv.substr(0, eq);
                        std::string v = kv.substr(eq + 1);
                        if (k == "code") code = v;
                    }
                    pos = amp + 1;
                }
            }

            const char* body_ok =
                "<html><body><h2>OK</h2>"
                "<p>You can close this window now.</p>"
                "</body></html>";
            const char* body_err =
                "<html><body><h2>Error</h2>"
                "<p>No code received.</p></body></html>";

            std::ostringstream resp;
            resp << "HTTP/1.1 " << (code.empty() ? "400" : "200") << " OK\r\n";
            resp << "Content-Type: text/html; charset=utf-8\r\n";
            resp << "Content-Length: "
                 << (code.empty() ? strlen(body_err) : strlen(body_ok)) << "\r\n";
            resp << "Connection: close\r\n\r\n";
            resp << (code.empty() ? body_err : body_ok);
            std::string r = resp.str();
            ::write(cfd, r.data(), r.size());
            ::close(cfd);

            if (!code.empty()) {
                {
                    std::lock_guard<std::mutex> g(mu_);
                    code_ = code;
                }
                cv_.notify_all();
                return;
            }
        }
    });
}

CallbackServer::~CallbackServer() {
    stop_.store(true);
    if (fd_ >= 0) {
        ::shutdown(fd_, SHUT_RDWR);
        ::close(fd_);
        fd_ = -1;
    }
    if (thread_.joinable()) thread_.join();
}

uint16_t CallbackServer::port() const { return port_; }

std::optional<std::string> CallbackServer::wait_for_code(int timeout_ms) {
    std::unique_lock<std::mutex> g(mu_);
    cv_.wait_for(g, std::chrono::milliseconds(timeout_ms),
                 [&] { return code_.has_value(); });
    return code_;
}

void CallbackServer::stop() { stop_.store(true); }

pi::core::Result<OAuthCredentials> exchange_code(
    const OAuthConfig& cfg, const std::string& code, const PKCE& pkce) {
    pi::http::HttpRequest req;
    req.method = "POST";
    req.url = cfg.token_endpoint;
    req.headers["content-type"] = "application/x-www-form-urlencoded";
    req.headers["accept"] = "application/json";

    std::ostringstream body;
    body << "grant_type=authorization_code";
    body << "&client_id=" << url_encode(cfg.client_id);
    body << "&code=" << url_encode(code);
    body << "&code_verifier=" << url_encode(pkce.verifier);
    body << "&redirect_uri=" << url_encode(
        "http://" + cfg.redirect_host + ":" + std::to_string(cfg.redirect_port) + "/callback");
    req.body = body.str();

    auto resp = pi::http::shared_client().send(req);
    if (!resp) return resp.error();
    if (resp.value().status >= 400) {
        return pi::core::make_error(pi::core::ErrorKind::Auth,
            "oauth: token exchange failed: HTTP " + std::to_string(resp.value().status)
            + ": " + resp.value().body.substr(0, std::min<size_t>(resp.value().body.size(), 200)));
    }
    auto j = pi::core::tryParse(resp.value().body);
    if (!j) {
        return pi::core::make_error(pi::core::ErrorKind::Auth,
            "oauth: token exchange returned invalid JSON");
    }
    OAuthCredentials c;
    c.access_token = j->value("access_token", std::string{});
    c.refresh_token = j->value("refresh_token", std::string{});
    int expires_in = j->value("expires_in", 0);
    if (expires_in > 0) {
        c.expires_at_ms = static_cast<int64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count())
            + static_cast<int64_t>(expires_in) * 1000;
    }
    if (j->contains("account")) c.account_id = (*j)["account"].get<std::string>();
    c.extra = *j;
    if (c.access_token.empty()) {
        return pi::core::make_error(pi::core::ErrorKind::Auth,
            "oauth: response missing access_token");
    }
    return c;
}

pi::core::Result<OAuthCredentials> refresh_token(
    const OAuthConfig& cfg, const std::string& refresh_token) {
    pi::http::HttpRequest req;
    req.method = "POST";
    req.url = cfg.token_endpoint;
    req.headers["content-type"] = "application/x-www-form-urlencoded";
    req.headers["accept"] = "application/json";

    std::ostringstream body;
    body << "grant_type=refresh_token";
    body << "&client_id=" << url_encode(cfg.client_id);
    body << "&refresh_token=" << url_encode(refresh_token);
    req.body = body.str();

    auto resp = pi::http::shared_client().send(req);
    if (!resp) return resp.error();
    if (resp.value().status >= 400) {
        return pi::core::make_error(pi::core::ErrorKind::Auth,
            "oauth: refresh failed: HTTP " + std::to_string(resp.value().status));
    }
    auto j = pi::core::tryParse(resp.value().body);
    if (!j) return pi::core::make_error(pi::core::ErrorKind::Auth,
        "oauth: refresh returned invalid JSON");
    OAuthCredentials c;
    c.access_token = j->value("access_token", std::string{});
    c.refresh_token = j->value("refresh_token", refresh_token);
    int expires_in = j->value("expires_in", 0);
    if (expires_in > 0) {
        c.expires_at_ms = static_cast<int64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count())
            + static_cast<int64_t>(expires_in) * 1000;
    }
    c.extra = *j;
    return c;
}

}  // namespace pi::coding
