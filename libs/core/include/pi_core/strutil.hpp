// pi_core/strutil.hpp - String utilities
#pragma once

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace pi::core::str {

inline std::string to_lower(std::string_view s) {
    std::string r(s);
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return r;
}

inline std::string to_upper(std::string_view s) {
    std::string r(s);
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return r;
}

inline bool starts_with(std::string_view s, std::string_view prefix) noexcept {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

inline bool ends_with(std::string_view s, std::string_view suffix) noexcept {
    return s.size() >= suffix.size()
           && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline bool contains(std::string_view haystack, std::string_view needle) noexcept {
    return haystack.find(needle) != std::string_view::npos;
}

inline std::string_view trim(std::string_view s) noexcept {
    auto is_ws = [](unsigned char c) { return std::isspace(c); };
    size_t b = 0, e = s.size();
    while (b < e && is_ws(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && is_ws(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

inline std::vector<std::string_view> split(std::string_view s, char delim) {
    std::vector<std::string_view> out;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == delim) {
            out.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}

inline std::vector<std::string_view> split_any(std::string_view s, std::string_view delims) {
    std::vector<std::string_view> out;
    size_t start = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (delims.find(s[i]) != std::string_view::npos) {
            out.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    out.push_back(s.substr(start));
    return out;
}

inline std::string join(const std::vector<std::string>& parts, std::string_view delim) {
    std::string r;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) r.append(delim);
        r.append(parts[i]);
    }
    return r;
}

inline std::string replace_all(std::string_view s, std::string_view from, std::string_view to) {
    if (from.empty()) return std::string(s);
    std::string r;
    r.reserve(s.size());
    size_t pos = 0;
    while (pos < s.size()) {
        auto hit = s.find(from, pos);
        if (hit == std::string_view::npos) {
            r.append(s.substr(pos));
            break;
        }
        r.append(s.substr(pos, hit - pos));
        r.append(to);
        pos = hit + from.size();
    }
    return r;
}

inline bool iequal(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i]))
            != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

}  // namespace pi::core::str
