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
    int thinking_dots_max = 3;

    static Theme dark();      // default
    static Theme light();
};

}  // namespace pi::tui
