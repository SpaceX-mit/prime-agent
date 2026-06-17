// libs/tui/src/theme.cpp
#include "pi_tui/theme.hpp"

namespace pi::tui {

Theme Theme::dark() {
    Theme t;
    t.primary     = "\x1b[38;5;252m";
    t.dim         = "\x1b[38;5;244m";
    t.accent      = "\x1b[38;5;110m";
    t.success     = "\x1b[38;5;114m";
    t.warning     = "\x1b[38;5;221m";
    t.error       = "\x1b[38;5;203m";
    t.border      = "\x1b[38;5;238m";
    t.user_bg     = "\x1b[48;5;24m";
    t.user_fg     = "\x1b[38;5;252m";
    t.assistant_bg = "";
    t.assistant_fg = "\x1b[38;5;252m";
    t.footer_bg   = "\x1b[48;5;236m";
    t.footer_fg   = "\x1b[38;5;246m";
    t.input_bg    = "\x1b[48;5;236m";
    t.input_fg    = "\x1b[38;5;252m";
    t.selection_bg = "\x1b[48;5;60m";
    t.selection_fg = "\x1b[38;5;231m";
    return t;
}

Theme Theme::light() {
    Theme t;
    t.primary     = "\x1b[38;5;236m";
    t.dim         = "\x1b[38;5;245m";
    t.accent      = "\x1b[38;5;24m";
    t.success     = "\x1b[38;5;28m";
    t.warning     = "\x1b[38;5;130m";
    t.error       = "\x1b[38;5;160m";
    t.border      = "\x1b[38;5;250m";
    t.user_bg     = "\x1b[48;5;153m";
    t.user_fg     = "\x1b[38;5;236m";
    t.assistant_bg = "";
    t.assistant_fg = "\x1b[38;5;236m";
    t.footer_bg   = "\x1b[48;5;252m";
    t.footer_fg   = "\x1b[38;5;240m";
    t.input_bg    = "\x1b[48;5;252m";
    t.input_fg    = "\x1b[38;5;236m";
    t.selection_bg = "\x1b[48;5;153m";
    t.selection_fg = "\x1b[38;5;236m";
    return t;
}

}  // namespace pi::tui
