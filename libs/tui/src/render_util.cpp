// libs/tui/src/render_util.cpp
#include "pi_tui/render_util.hpp"

#include "pi_core/unicode_width.hpp"

#include <string>

namespace pi::tui::render {

namespace {

// Skip one ANSI/OSC escape sequence starting at s[i] (s[i] == ESC).
// Returns the index just past the sequence. Handles:
//   CSI:  ESC [ ... <final 0x40-0x7E>
//   OSC:  ESC ] ... (BEL or ST)
//   APC:  ESC _ ... (BEL or ST)   (cursor markers)
//   2-char: ESC <byte>  (e.g. ESC 7 / ESC 8)
size_t skip_escape(std::string_view s, size_t i) {
    size_t n = s.size();
    if (i >= n || static_cast<unsigned char>(s[i]) != 0x1B) return i + 1;
    if (i + 1 >= n) return n;
    char c = s[i + 1];
    if (c == '[') {
        size_t j = i + 2;
        while (j < n && !(static_cast<unsigned char>(s[j]) >= 0x40 &&
                          static_cast<unsigned char>(s[j]) <= 0x7E))
            ++j;
        return j < n ? j + 1 : n;
    }
    if (c == ']' || c == '_' || c == 'P' || c == '^') {
        // String-terminated (OSC/APC/DCS/PM): ends at BEL or ESC \.
        size_t j = i + 2;
        while (j < n) {
            if (static_cast<unsigned char>(s[j]) == 0x07) return j + 1;  // BEL
            if (static_cast<unsigned char>(s[j]) == 0x1B && j + 1 < n && s[j + 1] == '\\')
                return j + 2;  // ST (ESC backslash)
            ++j;
        }
        return n;
    }
    // Two-byte escape (ESC 7, ESC 8, ESC c, ...).
    return i + 2;
}

// Length in bytes of the UTF-8 sequence starting at byte b.
int utf8_len(unsigned char b) {
    if (b < 0x80) return 1;
    if ((b & 0xE0) == 0xC0) return 2;
    if ((b & 0xF0) == 0xE0) return 3;
    if ((b & 0xF8) == 0xF0) return 4;
    return 1;
}

}  // namespace

int visible_width(std::string_view s) {
    int w = 0;
    size_t i = 0, n = s.size();
    while (i < n) {
        if (static_cast<unsigned char>(s[i]) == 0x1B) {
            i = skip_escape(s, i);
            continue;
        }
        int len = utf8_len(static_cast<unsigned char>(s[i]));
        if (i + static_cast<size_t>(len) > n) len = static_cast<int>(n - i);
        int cw = pi::core::unicode::display_width(s.substr(i, len));
        if (cw > 0) w += cw;  // ignore control(0) / invalid(-1)
        i += len;
    }
    return w;
}

std::string truncate_to_width(std::string_view s, int max_width,
                              std::string_view ellipsis) {
    if (max_width <= 0) return std::string();
    if (visible_width(s) <= max_width) return std::string(s);

    int ell_w = visible_width(ellipsis);
    int budget = max_width - ell_w;
    if (budget < 0) budget = 0;

    std::string out;
    int w = 0;
    size_t i = 0, n = s.size();
    while (i < n) {
        if (static_cast<unsigned char>(s[i]) == 0x1B) {
            size_t j = skip_escape(s, i);
            out.append(s.substr(i, j - i));  // keep styling, no width cost
            i = j;
            continue;
        }
        int len = utf8_len(static_cast<unsigned char>(s[i]));
        if (i + static_cast<size_t>(len) > n) len = static_cast<int>(n - i);
        int cw = pi::core::unicode::display_width(s.substr(i, len));
        if (cw < 0) cw = 1;
        if (w + cw > budget) break;
        out.append(s.substr(i, len));
        w += cw;
        i += len;
    }
    out.append("\x1b[0m");  // close any open style before the ellipsis
    out.append(ellipsis);
    return out;
}

std::string pad_to_width(std::string_view s, int width) {
    int vis = visible_width(s);
    std::string out(s);
    if (vis < width) out.append(static_cast<size_t>(width - vis), ' ');
    return out;
}

std::string apply_bg_to_line(std::string_view line, int width, std::string_view bg) {
    std::string padded = pad_to_width(line, width);
    if (bg.empty()) return padded;
    // The line may contain its own "\x1b[0m" resets (from fg styling or a
    // sender marker). A full reset also clears the background, which would
    // leave the rest of the row un-filled. Re-inject `bg` immediately after
    // every reset so the background persists across the entire padded row.
    const std::string reset = "\x1b[0m";
    std::string body;
    body.reserve(padded.size() + 32);
    size_t pos = 0;
    while (true) {
        size_t hit = padded.find(reset, pos);
        if (hit == std::string::npos) {
            body.append(padded, pos, std::string::npos);
            break;
        }
        body.append(padded, pos, hit - pos + reset.size());
        body.append(bg);  // re-apply background after the reset
        pos = hit + reset.size();
    }
    std::string out;
    out.reserve(bg.size() + body.size() + reset.size());
    out.append(bg);
    out.append(body);
    out.append(reset);
    return out;
}

}  // namespace pi::tui::render
