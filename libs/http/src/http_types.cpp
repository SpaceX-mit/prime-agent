// libs/http/src/http_types.cpp
#include "pi_http/http_types.hpp"
#include "pi_core/strutil.hpp"

#include <sstream>

namespace pi::http {

namespace {
std::string url_encode(std::string_view s) {
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
}  // namespace

std::string HttpRequest::full_url() const {
    if (query.empty()) return url;
    std::string out = url;
    out += (url.find('?') == std::string::npos) ? '?' : '&';
    bool first = true;
    for (auto& [k, v] : query) {
        if (!first) out += '&';
        first = false;
        out += url_encode(k);
        out += '=';
        out += url_encode(v);
    }
    return out;
}

}  // namespace pi::http
