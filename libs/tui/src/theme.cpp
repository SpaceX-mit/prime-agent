// libs/tui/src/theme.cpp
// Ported from upstream pi dark.json / light.json. Colors are emitted as
// truecolor SGR. Backgrounds use 48;2;r;g;b; foregrounds use 38;2;r;g;b.
#include "pi_tui/theme.hpp"

namespace pi::tui {

namespace {
// Truecolor foreground / background builders.
inline std::string fg(int r, int g, int b) {
    return "\x1b[38;2;" + std::to_string(r) + ";" + std::to_string(g) + ";" + std::to_string(b) + "m";
}
inline std::string bg(int r, int g, int b) {
    return "\x1b[48;2;" + std::to_string(r) + ";" + std::to_string(g) + ";" + std::to_string(b) + "m";
}
}  // namespace

Theme Theme::dark() {
    Theme t;
    // vars (dark.json): text #d4d4d4, gray #808080, dimGray #666666,
    // darkGray #505050, accent #8abeb7, blue #5f87ff, cyan #00d7ff,
    // green #b5bd68, red #cc6666, yellow #ffff00.
    t.accent        = fg(0x8a, 0xbe, 0xb7);
    t.border        = fg(0x5f, 0x87, 0xff);
    t.border_accent = fg(0x00, 0xd7, 0xff);
    t.border_muted  = fg(0x50, 0x50, 0x50);
    t.success       = fg(0xb5, 0xbd, 0x68);
    t.warning       = fg(0xff, 0xff, 0x00);
    t.error         = fg(0xcc, 0x66, 0x66);
    t.muted         = fg(0x80, 0x80, 0x80);
    t.dim           = fg(0x66, 0x66, 0x66);
    t.text          = fg(0xd4, 0xd4, 0xd4);
    t.thinking_text = fg(0x80, 0x80, 0x80);  // gray

    t.selected_bg          = bg(0x3a, 0x3a, 0x4a);
    t.user_message_bg      = bg(0x34, 0x35, 0x41);
    t.user_message_text    = fg(0xd4, 0xd4, 0xd4);
    t.custom_message_bg    = bg(0x2d, 0x28, 0x38);
    t.custom_message_text  = fg(0xd4, 0xd4, 0xd4);
    t.custom_message_label = fg(0x95, 0x75, 0xcd);
    t.tool_pending_bg      = bg(0x28, 0x28, 0x32);
    t.tool_success_bg      = bg(0x28, 0x32, 0x28);
    t.tool_error_bg        = bg(0x3c, 0x28, 0x28);
    t.tool_title           = fg(0xd4, 0xd4, 0xd4);
    t.tool_output          = fg(0x80, 0x80, 0x80);

    t.md_heading     = fg(0xf0, 0xc6, 0x74);
    t.md_link        = fg(0x81, 0xa2, 0xbe);
    t.md_code        = fg(0x8a, 0xbe, 0xb7);
    t.md_code_block  = fg(0xb5, 0xbd, 0x68);
    t.md_list_bullet = fg(0x8a, 0xbe, 0xb7);

    t.tool_diff_added   = fg(0xb5, 0xbd, 0x68);  // green
    t.tool_diff_removed = fg(0xcc, 0x66, 0x66);  // red
    t.tool_diff_context = fg(0x80, 0x80, 0x80);  // gray

    t.bash_mode = fg(0xb5, 0xbd, 0x68);  // green

    // Back-compat aliases.
    t.primary      = t.text;
    t.user_label   = "\x1b[1m" + t.accent;  // bold accent
    t.tool_pending = t.warning;
    t.tool_ok      = t.success;
    t.tool_err     = t.error;
    t.thinking     = t.thinking_text;
    t.input_bg     = "";              // main-screen CLI: no input bg fill
    t.input_fg     = t.text;
    t.footer_bg    = "";
    t.footer_fg    = t.dim;
    return t;
}

Theme Theme::light() {
    Theme t;
    // A muted light palette (upstream light.json analog).
    t.accent        = fg(0x2a, 0x7a, 0x6f);
    t.border        = fg(0x30, 0x5f, 0xcf);
    t.border_accent = fg(0x00, 0x87, 0xaf);
    t.border_muted  = fg(0xb0, 0xb0, 0xb0);
    t.success       = fg(0x2e, 0x7d, 0x32);
    t.warning       = fg(0xb8, 0x86, 0x00);
    t.error         = fg(0xc0, 0x30, 0x30);
    t.muted         = fg(0x70, 0x70, 0x70);
    t.dim           = fg(0x90, 0x90, 0x90);
    t.text          = fg(0x20, 0x20, 0x20);
    t.thinking_text = fg(0x70, 0x70, 0x70);

    t.selected_bg          = bg(0xd0, 0xd8, 0xf0);
    t.user_message_bg      = bg(0xe8, 0xea, 0xf2);
    t.user_message_text    = fg(0x20, 0x20, 0x20);
    t.custom_message_bg    = bg(0xee, 0xe8, 0xf6);
    t.custom_message_text  = fg(0x20, 0x20, 0x20);
    t.custom_message_label = fg(0x6a, 0x3d, 0xa8);
    t.tool_pending_bg      = bg(0xee, 0xee, 0xf4);
    t.tool_success_bg      = bg(0xe6, 0xf2, 0xe6);
    t.tool_error_bg        = bg(0xf6, 0xe6, 0xe6);
    t.tool_title           = fg(0x20, 0x20, 0x20);
    t.tool_output          = fg(0x60, 0x60, 0x60);

    t.md_heading     = fg(0xb8, 0x86, 0x00);
    t.md_link        = fg(0x30, 0x5f, 0xcf);
    t.md_code        = fg(0x2a, 0x7a, 0x6f);
    t.md_code_block  = fg(0x2e, 0x7d, 0x32);
    t.md_list_bullet = fg(0x2a, 0x7a, 0x6f);

    t.tool_diff_added   = fg(0x2e, 0x7d, 0x32);
    t.tool_diff_removed = fg(0xc0, 0x30, 0x30);
    t.tool_diff_context = fg(0x70, 0x70, 0x70);

    t.bash_mode = fg(0x2e, 0x7d, 0x32);

    t.primary      = t.text;
    t.user_label   = "\x1b[1m" + t.accent;
    t.tool_pending = t.warning;
    t.tool_ok      = t.success;
    t.tool_err     = t.error;
    t.thinking     = t.thinking_text;
    t.input_bg     = "";
    t.input_fg     = t.text;
    t.footer_bg    = "";
    t.footer_fg    = t.dim;
    return t;
}

}  // namespace pi::tui
