// tests/test_render.cpp
// Visual-parity unit tests for the TUI render layer: width-aware helpers,
// theme tokens (truecolor), and message renderers (user/tool/diff/thinking).
#define TEST_FRAMEWORK_IMPLEMENT
#include "test_framework.hpp"

#include "pi_tui/message_render.hpp"
#include "pi_tui/render_util.hpp"
#include "pi_tui/theme.hpp"

using namespace pi::tui;
using namespace pi::tui::render;

static bool contains(const std::string& h, const std::string& n) {
    return h.find(n) != std::string::npos;
}

TEST_CASE("visible_width ignores ANSI + counts CJK as 2") {
    CHECK(visible_width("hello") == 5);
    CHECK(visible_width("\x1b[38;2;1;2;3mhi\x1b[0m") == 2);
    CHECK(visible_width("你好") == 4);          // 你好
    CHECK(visible_width("\x1b]133;A\x07X") == 1);        // OSC skipped
}

TEST_CASE("truncate_to_width caps visible width") {
    CHECK(visible_width(truncate_to_width("abcdefgh", 5, "...")) <= 5);
    CHECK(truncate_to_width("abc", 10, "...") == "abc");  // no-op when fits
}

TEST_CASE("pad_to_width and apply_bg_to_line reach exact width") {
    CHECK(visible_width(pad_to_width("ab", 6)) == 6);
    auto bgl = apply_bg_to_line("hi", 8, "\x1b[48;2;1;2;3m");
    CHECK(visible_width(bgl) == 8);
    CHECK(contains(bgl, "\x1b[48;2;1;2;3m"));  // bg present
}

TEST_CASE("dark theme uses upstream truecolor palette") {
    Theme t = Theme::dark();
    CHECK(t.user_message_bg == "\x1b[48;2;52;53;65m");   // #343541
    CHECK(t.tool_success_bg == "\x1b[48;2;40;50;40m");   // #283228
    CHECK(t.tool_error_bg  == "\x1b[48;2;60;40;40m");    // #3c2828
    CHECK(t.tool_pending_bg == "\x1b[48;2;40;40;50m");   // #282832
    CHECK(t.tool_diff_added == "\x1b[38;2;181;189;104m"); // green #b5bd68
    CHECK(t.tool_diff_removed == "\x1b[38;2;204;102;102m"); // red #cc6666
    CHECK(t.custom_message_label == "\x1b[38;2;149;117;205m"); // #9575cd
}

TEST_CASE("user_message: bg spans full width, padded box") {
    Theme t = Theme::dark();
    auto block = msg::user_message("hello", t, 20);
    // Every rendered row is padded to width 20 (bg fill).
    size_t start = 0;
    int rows = 0;
    while (start <= block.size()) {
        size_t nl = block.find('\n', start);
        std::string row = block.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        CHECK(visible_width(row) == 20);
        rows++;
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    CHECK(rows == 1);  // padY=0 → single content row, bg only on text line
    CHECK(contains(block, t.user_message_bg));
    CHECK(contains(block, "\xE2\x80\xBA"));   // "›" sender marker present
}

TEST_CASE("tool_execution: state selects background color") {
    Theme t = Theme::dark();
    CHECK(contains(msg::tool_execution("bash", "ls", msg::ToolState::Pending, t, 30), t.tool_pending_bg));
    CHECK(contains(msg::tool_execution("bash", "ok", msg::ToolState::Success, t, 30), t.tool_success_bg));
    CHECK(contains(msg::tool_execution("bash", "boom", msg::ToolState::Error, t, 30), t.tool_error_bg));
    // bold title present
    CHECK(contains(msg::tool_execution("bash", "", msg::ToolState::Success, t, 30), "\x1b[1m"));
}

TEST_CASE("diff_line: +/-/space pick correct colors") {
    Theme t = Theme::dark();
    CHECK(contains(msg::diff_line("+added", t), t.tool_diff_added));
    CHECK(contains(msg::diff_line("-removed", t), t.tool_diff_removed));
    CHECK(contains(msg::diff_line(" context", t), t.tool_diff_context));
}

TEST_CASE("thinking_text: italic + thinkingText color") {
    Theme t = Theme::dark();
    auto s = msg::thinking_text("reasoning", t, 40);
    CHECK(contains(s, "\x1b[3m"));            // italic
    CHECK(contains(s, t.thinking_text));      // gray
}
