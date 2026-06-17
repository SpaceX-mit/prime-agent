// libs/tui/include/pi_tui/theme.hpp
// Theme: color and style definitions.

#pragma once

#include <string>

namespace pi::tui {

struct Theme {
    std::string primary;     // default text
    std::string dim;
    std::string accent;
    std::string success;
    std::string warning;
    std::string error;
    std::string border;
    std::string user_bg;
    std::string user_fg;
    std::string assistant_bg;
    std::string assistant_fg;
    std::string footer_bg;
    std::string footer_fg;
    std::string input_bg;
    std::string input_fg;
    std::string selection_bg;
    std::string selection_fg;
    // Phase-specific tokens (mirrors upstream pi's dark theme buckets).
    std::string thinking;       // reasoning stream — muted violet
    std::string tool_pending;   // tool call announced, not yet finished
    std::string tool_ok;        // tool result success
    std::string tool_err;       // tool result error
    std::string user_label;     // "›" prefix on user prompt (was previously inline accent)
    int thinking_dots_max = 3;

    static Theme dark();      // default
    static Theme light();
};

}  // namespace pi::tui
