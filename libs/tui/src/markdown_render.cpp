// libs/tui/src/markdown_render.cpp
#include "pi_tui/markdown_render.hpp"

#include <sstream>
#include <string>
#include <vector>

namespace pi::tui::md {

namespace {

std::vector<std::string> split_lines(std::string_view s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == '\n') { out.push_back(cur); cur.clear(); }
        else if (c != '\r') cur.push_back(c);
    }
    out.push_back(cur);
    return out;
}

std::string rstrip(const std::string& s) {
    size_t e = s.size();
    while (e > 0 && (s[e-1] == ' ' || s[e-1] == '\t')) --e;
    return s.substr(0, e);
}

size_t indent_of(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return i;
}

bool is_hr(const std::string& s) {
    std::string t;
    for (char c : s) if (c != ' ') t.push_back(c);
    if (t.size() < 3) return false;
    char c0 = t[0];
    if (c0 != '-' && c0 != '*' && c0 != '_') return false;
    for (char c : t) if (c != c0) return false;
    return true;
}

}  // namespace

std::string render_inline(std::string_view text, const Theme& t) {
    // Single pass over the inline string, recognizing the common spans.
    // Each span emits its style + content + reset, so nesting is shallow
    // (matches terminal markdown rendering needs).
    std::string out;
    size_t i = 0, n = text.size();
    auto starts = [&](const char* p, size_t at) {
        size_t k = 0; while (p[k]) { if (at + k >= n || text[at+k] != p[k]) return false; ++k; }
        return true;
    };
    while (i < n) {
        char c = text[i];
        // Inline code: `...`
        if (c == '`') {
            size_t end = text.find('`', i + 1);
            if (end != std::string_view::npos) {
                out += t.md_code;
                out.append(text.substr(i + 1, end - i - 1));
                out += "\x1b[0m";
                i = end + 1;
                continue;
            }
        }
        // Bold: **...**
        if (starts("**", i)) {
            size_t end = text.find("**", i + 2);
            if (end != std::string_view::npos) {
                out += "\x1b[1m";
                out += render_inline(text.substr(i + 2, end - i - 2), t);
                out += "\x1b[22m";
                i = end + 2;
                continue;
            }
        }
        // Strikethrough: ~~...~~
        if (starts("~~", i)) {
            size_t end = text.find("~~", i + 2);
            if (end != std::string_view::npos) {
                out += "\x1b[9m";
                out += render_inline(text.substr(i + 2, end - i - 2), t);
                out += "\x1b[29m";
                i = end + 2;
                continue;
            }
        }
        // Emphasis: *...* or _..._
        if ((c == '*' || c == '_') && i + 1 < n && text[i+1] != ' ' && text[i+1] != c) {
            size_t end = text.find(c, i + 1);
            if (end != std::string_view::npos && end > i + 1) {
                out += "\x1b[3m";
                out += render_inline(text.substr(i + 1, end - i - 1), t);
                out += "\x1b[23m";
                i = end + 1;
                continue;
            }
        }
        // Link: [text](url)
        if (c == '[') {
            size_t close = text.find(']', i + 1);
            if (close != std::string_view::npos && close + 1 < n && text[close+1] == '(') {
                size_t urlend = text.find(')', close + 2);
                if (urlend != std::string_view::npos) {
                    std::string_view label = text.substr(i + 1, close - i - 1);
                    std::string_view url = text.substr(close + 2, urlend - close - 2);
                    out += t.md_link;
                    out += "\x1b[4m";  // underline
                    out += render_inline(label, t);
                    out += "\x1b[24m\x1b[0m";
                    if (label != url) {
                        out += t.dim;
                        out += " (";
                        out.append(url);
                        out += ")\x1b[0m";
                    }
                    i = urlend + 1;
                    continue;
                }
            }
        }
        out.push_back(c);
        ++i;
    }
    return out;
}

std::string render(std::string_view markdown, const Theme& t, int width) {
    auto lines = split_lines(markdown);
    std::vector<std::string> out;
    bool in_fence = false;
    std::string fence_marker;

    for (size_t li = 0; li < lines.size(); ++li) {
        const std::string& raw = lines[li];
        std::string line = rstrip(raw);
        std::string trimmed = line.substr(std::min(indent_of(line), line.size()));

        // Fenced code block toggle.
        if (trimmed.rfind("```", 0) == 0 || trimmed.rfind("~~~", 0) == 0) {
            std::string marker = trimmed.substr(0, 3);
            if (!in_fence) {
                in_fence = true; fence_marker = marker;
                out.push_back(t.md_code_block + trimmed + "\x1b[0m");
            } else if (trimmed.rfind(fence_marker, 0) == 0) {
                in_fence = false;
                out.push_back(t.md_code_block + marker + "\x1b[0m");
            } else {
                out.push_back("  " + t.md_code_block + line + "\x1b[0m");
            }
            continue;
        }
        if (in_fence) {
            out.push_back("  " + t.md_code_block + line + "\x1b[0m");
            continue;
        }

        // Blank line.
        if (trimmed.empty()) { out.push_back(""); continue; }

        // Horizontal rule.
        if (is_hr(trimmed)) {
            int w = width > 0 ? width : 40;
            out.push_back(t.dim + std::string(static_cast<size_t>(w), '-') + "\x1b[0m");
            continue;
        }

        // ATX heading.
        if (trimmed[0] == '#') {
            size_t level = 0;
            while (level < trimmed.size() && trimmed[level] == '#') ++level;
            if (level >= 1 && level <= 6 && level < trimmed.size() && trimmed[level] == ' ') {
                std::string htext = trimmed.substr(level + 1);
                std::string styled = t.md_heading + "\x1b[1m";  // bold heading color
                if (level == 1) styled += "\x1b[4m";            // underline for H1
                styled += render_inline(htext, t);
                styled += "\x1b[0m";
                std::string prefix = (level >= 3)
                    ? t.md_heading + std::string(level, '#') + " \x1b[0m" : "";
                out.push_back(prefix + styled);
                continue;
            }
        }

        // Blockquote.
        if (trimmed[0] == '>') {
            std::string qt = trimmed.substr(1);
            if (!qt.empty() && qt[0] == ' ') qt = qt.substr(1);
            out.push_back(t.dim + "\xE2\x96\x8E " + render_inline(qt, t) + "\x1b[0m");  // ▎ quote bar
            continue;
        }

        // Unordered list.
        if ((trimmed[0] == '-' || trimmed[0] == '*' || trimmed[0] == '+') &&
            trimmed.size() > 1 && trimmed[1] == ' ') {
            std::string item = trimmed.substr(2);
            std::string lead(indent_of(line), ' ');
            out.push_back(lead + t.md_list_bullet + "\xE2\x80\xA2 \x1b[0m" + render_inline(item, t));  // •
            continue;
        }

        // Ordered list (e.g. "1. ").
        {
            size_t k = 0;
            while (k < trimmed.size() && trimmed[k] >= '0' && trimmed[k] <= '9') ++k;
            if (k > 0 && k + 1 < trimmed.size() && trimmed[k] == '.' && trimmed[k+1] == ' ') {
                std::string num = trimmed.substr(0, k);
                std::string item = trimmed.substr(k + 2);
                std::string lead(indent_of(line), ' ');
                out.push_back(lead + t.md_list_bullet + num + ". \x1b[0m" + render_inline(item, t));
                continue;
            }
        }

        // Paragraph text.
        out.push_back(render_inline(line, t));
    }

    std::ostringstream o;
    for (size_t i = 0; i < out.size(); ++i) { if (i) o << '\n'; o << out[i]; }
    return o.str();
}

}  // namespace pi::tui::md
