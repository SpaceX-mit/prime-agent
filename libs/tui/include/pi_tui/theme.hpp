// libs/tui/include/pi_tui/theme.hpp
// Theme: color and style definitions.
//
// Ported to match upstream pi's theme token model (see
// packages/coding-agent/src/modes/interactive/theme/dark.json). Colors are
// stored as ready-to-emit ANSI SGR strings (truecolor \x1b[38;2;r;g;bm).
// fg()/bg()/bold()/italic()/inverse() wrap a string in the token's style
// and append the matching reset, mirroring upstream's theme.fg(token,text).

#pragma once

#include <string>
#include <string_view>

namespace pi::tui {

struct Theme {
    // --- Core UI ---
    std::string accent;
    std::string border;
    std::string border_accent;
    std::string border_muted;
    std::string success;
    std::string warning;
    std::string error;
    std::string muted;
    std::string dim;
    std::string text;          // default body text
    std::string thinking_text; // reasoning stream

    // --- Backgrounds & content text ---
    std::string selected_bg;
    std::string user_message_bg;
    std::string user_message_text;
    std::string custom_message_bg;
    std::string custom_message_text;
    std::string custom_message_label;
    std::string tool_pending_bg;
    std::string tool_success_bg;
    std::string tool_error_bg;
    std::string tool_title;
    std::string tool_output;

    // --- Markdown ---
    std::string md_heading;
    std::string md_link;
    std::string md_code;
    std::string md_code_block;
    std::string md_list_bullet;

    // --- Tool diffs ---
    std::string tool_diff_added;
    std::string tool_diff_removed;
    std::string tool_diff_context;

    // --- Bash mode ---
    std::string bash_mode;

    // --- Back-compat aliases (used by input.cpp / footer.cpp / older paths).
    // Mapped to the corresponding upstream token in Theme::dark()/light().
    std::string primary;      // == text
    std::string user_label;   // bold accent "›" prefix
    std::string tool_pending; // == warning / toolTitle pending
    std::string tool_ok;      // == success
    std::string tool_err;     // == error
    std::string thinking;     // == thinking_text (with italic baked in by caller)
    std::string input_bg;
    std::string input_fg;
    std::string footer_bg;
    std::string footer_fg;

    // ------------------------------------------------------------------
    // Style helpers: wrap `s` in the SGR for `color` and append a reset.
    // `color` is a foreground SGR string (e.g. this->error). bg() converts
    // the leading 38; (foreground) intent into 48; (background) is NOT done
    // here — background tokens are stored as 48;2;... directly.
    // ------------------------------------------------------------------
    static std::string wrap(std::string_view sgr, std::string_view s) {
        if (sgr.empty()) return std::string(s);
        std::string out;
        out.reserve(sgr.size() + s.size() + 4);
        out.append(sgr);
        out.append(s);
        out.append("\x1b[0m");
        return out;
    }
    static std::string bold(std::string_view s)    { return "\x1b[1m" + std::string(s) + "\x1b[22m"; }
    static std::string italic(std::string_view s)  { return "\x1b[3m" + std::string(s) + "\x1b[23m"; }
    static std::string inverse(std::string_view s) { return "\x1b[7m" + std::string(s) + "\x1b[27m"; }

    static Theme dark();      // default
    static Theme light();
};

}  // namespace pi::tui
