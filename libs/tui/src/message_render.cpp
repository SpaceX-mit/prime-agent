// libs/tui/src/message_render.cpp
#include "pi_tui/message_render.hpp"

#include "pi_tui/render_util.hpp"

#include <sstream>
#include <string>
#include <vector>

namespace pi::tui::msg {

namespace {

using render::apply_bg_to_line;
using render::pad_to_width;
using render::visible_width;

// Split text into lines on '\n' (keeping empty lines). Trailing newline does
// not create an extra trailing empty element.
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

// Trim leading/trailing blank lines + surrounding whitespace like upstream's
// text.trim() before rendering a message body.
std::string trim(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\n' || s[a] == '\r' || s[a] == '\t')) ++a;
    while (b > a && (s[b-1] == ' ' || s[b-1] == '\n' || s[b-1] == '\r' || s[b-1] == '\t')) --b;
    return std::string(s.substr(a, b - a));
}

// A box with padX/padY filled with `bg`, content lines in `fg`. Mirrors
// upstream Box(padX, padY, bgFn) + Text(fg). Each row padded to `width`.
std::string boxed(const std::vector<std::string>& content_lines,
                  int pad_x, int pad_y, std::string_view bg, std::string_view fg,
                  int width, bool italic) {
    int inner = width - pad_x * 2;
    if (inner < 1) inner = 1;
    std::string left(static_cast<size_t>(pad_x), ' ');
    std::vector<std::string> rows;

    // top padding rows (blank, just background)
    for (int i = 0; i < pad_y; ++i) rows.push_back("");

    for (auto& ln : content_lines) {
        // Style the visible content; padding spaces are added by apply_bg.
        std::string styled;
        if (!fg.empty()) styled += fg;
        if (italic) styled += "\x1b[3m";
        styled += ln;
        styled += "\x1b[0m";
        rows.push_back(left + styled);
    }

    for (int i = 0; i < pad_y; ++i) rows.push_back("");

    std::ostringstream o;
    for (size_t i = 0; i < rows.size(); ++i) {
        if (i) o << '\n';
        o << apply_bg_to_line(rows[i], width, bg);
    }
    return o.str();
}

}  // namespace

std::string user_message(std::string_view text, const Theme& t, int width) {
    auto lines = split_lines(trim(text));
    // Prefix a sender marker on the first line ("› ") so it's unmistakably a
    // user-sent message; continuation lines are indented by 2 cols to align
    // under the text. The marker is bold accent over the user-message bg.
    std::string marker = "\x1b[1m" + t.accent + "\xE2\x80\xBA \x1b[0m" + t.user_message_text;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i == 0) lines[i] = marker + lines[i];
        else        lines[i] = "  " + lines[i];  // align under text after "› "
    }
    return boxed(lines, /*padX*/1, /*padY*/0,
                 t.user_message_bg, t.user_message_text, width, /*italic*/false);
}

std::string assistant_text(std::string_view text, const Theme& t, int width) {
    auto lines = split_lines(text);
    std::ostringstream o;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i) o << '\n';
        o << " " << t.text << lines[i] << "\x1b[0m";  // padding x=1, no bg
    }
    (void)width;
    return o.str();
}

std::string thinking_text(std::string_view text, const Theme& t, int width) {
    auto lines = split_lines(text);
    std::ostringstream o;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i) o << '\n';
        o << " \x1b[3m" << t.thinking_text << lines[i] << "\x1b[0m";  // italic, pad x=1
    }
    (void)width;
    return o.str();
}

std::string tool_execution(std::string_view title, std::string_view detail,
                           ToolState state, const Theme& t, int width) {
    std::string_view bg = t.tool_success_bg;
    if (state == ToolState::Pending) bg = t.tool_pending_bg;
    else if (state == ToolState::Error) bg = t.tool_error_bg;

    // Title line: bold toolTitle.  Detail (optional): toolOutput.
    std::string title_line = "\x1b[1m" + t.tool_title + std::string(title) + "\x1b[0m";
    std::vector<std::string> content;
    content.push_back(title_line);
    if (!detail.empty())
        content.push_back(t.tool_output + std::string(detail) + "\x1b[0m");

    // Spacer(1) above, like upstream, then the box. The spacer is a blank
    // (no-bg) line so consecutive tools are visually separated.
    std::string box = boxed(content, 1, 1, bg, /*fg already inline*/"", width, false);
    return "\n" + box;
}

std::string diff_line(std::string_view raw, const Theme& t) {
    if (raw.empty()) return std::string();
    char sign = raw[0];
    std::string_view color =
        sign == '+' ? std::string_view(t.tool_diff_added)
      : sign == '-' ? std::string_view(t.tool_diff_removed)
                    : std::string_view(t.tool_diff_context);
    return std::string(color) + std::string(raw) + "\x1b[0m";
}

std::string compaction_message(std::string_view summary, int dropped,
                               const Theme& t, int width) {
    std::vector<std::string> content;
    // Bold [compaction] label in customMessageLabel, then a summary header.
    std::ostringstream head;
    head << t.custom_message_label << "\x1b[1m[compaction]\x1b[22m\x1b[0m"
         << t.custom_message_text << "  compacted " << dropped << " earlier messages";
    content.push_back(head.str());
    if (!summary.empty()) {
        content.push_back("");
        for (auto& ln : split_lines(trim(summary)))
            content.push_back(t.custom_message_text + ln);
    }
    return boxed(content, 1, 1, t.custom_message_bg, "", width, false);
}

}  // namespace pi::tui::msg
