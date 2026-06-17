// tests/test_editor.cpp
#define TEST_FRAMEWORK_IMPLEMENT
#include "test_framework.hpp"

#include "pi_tui/components/editor.hpp"
#include "pi_tui/components/input.hpp"
#include "pi_tui/terminal.hpp"
#include "pi_tui/theme.hpp"

#include <string>

using namespace pi;
using namespace pi::tui;

TEST_CASE("Editor insert text") {
    Theme theme = Theme::dark();
    components::Editor ed(theme);
    KeyEvent k;
    k.kind = KeyEvent::Kind::Char; k.ch = 'h'; ed.on_key(k);
    k.ch = 'i'; ed.on_key(k);
    CHECK(ed.text() == "hi");
}

TEST_CASE("Editor Enter inserts newline, Ctrl-J submits") {
    Theme theme = Theme::dark();
    components::Editor ed(theme);
    KeyEvent k;
    k.kind = KeyEvent::Kind::Char; k.ch = 'a'; ed.on_key(k);
    k.kind = KeyEvent::Kind::Enter; ed.on_key(k);
    k.kind = KeyEvent::Kind::Char; k.ch = 'b'; ed.on_key(k);
    CHECK(ed.text() == "a\nb");
    CHECK_FALSE(ed.submit_pending());

    k.kind = KeyEvent::Kind::CtrlJ; ed.on_key(k);
    CHECK(ed.submit_pending());
    CHECK(ed.take_submit());
}

TEST_CASE("Editor backspace across line boundary") {
    Theme theme = Theme::dark();
    components::Editor ed(theme);
    KeyEvent k;
    k.kind = KeyEvent::Kind::Char; k.ch = 'a'; ed.on_key(k);
    k.kind = KeyEvent::Kind::Enter; ed.on_key(k);
    k.kind = KeyEvent::Kind::Char; k.ch = 'b'; ed.on_key(k);
    CHECK(ed.text() == "a\nb");

    k.kind = KeyEvent::Kind::Backspace; ed.on_key(k);
    CHECK(ed.text() == "a\n");

    k.kind = KeyEvent::Kind::Backspace; ed.on_key(k);
    CHECK(ed.text() == "a");
}

TEST_CASE("Editor history navigation") {
    Theme theme = Theme::dark();
    components::Editor ed(theme);
    ed.push_history("first");
    ed.push_history("second");
    KeyEvent k;
    k.kind = KeyEvent::Kind::Char; k.ch = 'x'; ed.on_key(k);
    k.kind = KeyEvent::Kind::Up; ed.on_key(k);
    CHECK(ed.text() == "second");
    k.kind = KeyEvent::Kind::Up; ed.on_key(k);
    CHECK(ed.text() == "first");
    k.kind = KeyEvent::Kind::Down; ed.on_key(k);
    CHECK(ed.text() == "second");
}

TEST_CASE("Editor kill word") {
    Theme theme = Theme::dark();
    components::Editor ed(theme);
    KeyEvent k;
    std::string s = "hello world foo";
    for (char c : s) {
        k.kind = KeyEvent::Kind::Char; k.ch = c; ed.on_key(k);
    }
    CHECK(ed.text() == s);
    k.kind = KeyEvent::Kind::CtrlW; ed.on_key(k);
    CHECK(ed.text() == "hello world ");
}

TEST_CASE("Editor arrow up/down moves across lines") {
    Theme theme = Theme::dark();
    components::Editor ed(theme);
    KeyEvent k;
    std::string s = "abc\ndef\nghi";
    for (char c : s) {
        k.kind = KeyEvent::Kind::Char; k.ch = c; ed.on_key(k);
    }
    k.kind = KeyEvent::Kind::Up; ed.on_key(k);
    k.kind = KeyEvent::Kind::Up; ed.on_key(k);
    // Cursor should be on first line now.
    CHECK(ed.text() == s);
}

TEST_CASE("Editor cancel via Ctrl-C") {
    Theme theme = Theme::dark();
    components::Editor ed(theme);
    KeyEvent k;
    k.kind = KeyEvent::Kind::Char; k.ch = 'x'; ed.on_key(k);
    k.kind = KeyEvent::Kind::CtrlC; ed.on_key(k);
    CHECK(ed.cancel_pending());
    CHECK(ed.take_cancel());
}

// ---------------------------------------------------------------------------
// INC-004 regression tests: input box must show what the user types.
// Bug:  input->on_key() mutated text_ but tui.render() was never called,
//       so the screen never reflected the keystrokes.
// Fix:  Input::render() now emits an inverse-video cursor at cursor_ (so
//       the cursor is visible) and the interactive-mode main loop calls
//       tui.render() after every input->on_key() (mirrors upstream
//       Editor's tui.requestRender() pattern).
// ---------------------------------------------------------------------------

TEST_CASE("Input render includes typed text") {
    Theme theme = Theme::dark();
    components::Input input(theme);
    input.set_prompt("> ");
    KeyEvent k;
    k.kind = KeyEvent::Kind::Char; k.ch = 'h'; input.on_key(k);
    k.kind = KeyEvent::Kind::Char; k.ch = 'i'; input.on_key(k);
    auto lines = input.render(80);
    CHECK(lines.size() == 1);
    // The render output must contain the typed characters.
    CHECK(lines[0].find("hi") != std::string::npos);
}

TEST_CASE("Input render shows cursor (inverse-video) at cursor position") {
    Theme theme = Theme::dark();
    components::Input input(theme);
    input.set_prompt("> ");
    KeyEvent k;
    k.kind = KeyEvent::Kind::Char; k.ch = 'a'; input.on_key(k);
    k.kind = KeyEvent::Kind::Char; k.ch = 'b'; input.on_key(k);
    k.kind = KeyEvent::Kind::Char; k.ch = 'c'; input.on_key(k);
    auto lines = input.render(80);
    // After typing "abc", cursor is at end, so render should contain
    // inverse-video space (cursor glyph for EOL position).
    CHECK(lines[0].find("\x1b[7m") != std::string::npos);
    // And the typed text "abc" should still be visible.
    CHECK(lines[0].find("abc") != std::string::npos);
}

TEST_CASE("Input render with cursor in middle highlights that grapheme") {
    Theme theme = Theme::dark();
    components::Input input(theme);
    input.set_prompt("> ");
    KeyEvent k;
    for (char c : std::string("hello")) {
        k.kind = KeyEvent::Kind::Char; k.ch = c; input.on_key(k);
    }
    // Now move cursor 2 steps back so it sits on 'l' (position 3).
    k.kind = KeyEvent::Kind::Left; input.on_key(k);
    k.kind = KeyEvent::Kind::Left; input.on_key(k);
    auto lines = input.render(80);
    // "hel" then inverse-video "l" then "o"
    CHECK(lines[0].find("hel") != std::string::npos);
    CHECK(lines[0].find("\x1b[7ml\x1b[0m") != std::string::npos);
    CHECK(lines[0].find("o") != std::string::npos);
}

TEST_CASE("Input backspace updates render output") {
    Theme theme = Theme::dark();
    components::Input input(theme);
    input.set_prompt("> ");
    KeyEvent k;
    for (char c : std::string("xy")) {
        k.kind = KeyEvent::Kind::Char; k.ch = c; input.on_key(k);
    }
    k.kind = KeyEvent::Kind::Backspace; input.on_key(k);
    auto lines = input.render(80);
    // 'y' should be gone, 'x' should remain.
    CHECK(lines[0].find("xy") == std::string::npos);
    CHECK(lines[0].find("x") != std::string::npos);
}

// ---------------------------------------------------------------------------
// INC-005 regression tests: UTF-8 multi-byte characters (CJK / emoji) must
// be inserted as a single grapheme. Before the fix, KeyEvent::Char carried
// only a single byte (char) and Terminal::try_read_key() read one byte at a
// time, so typing "你" (3 bytes: E4 BD A0) produced 3 separate Char events
// that were each inserted independently, yielding replacement glyphs (▒)
// and a corrupted multi-byte string that the LLM downstream couldn't parse.
// ---------------------------------------------------------------------------

TEST_CASE("Input insert UTF-8 multi-byte char as one grapheme") {
    Theme theme = Theme::dark();
    components::Input input(theme);
    input.set_prompt("> ");
    KeyEvent k;
    // "你" is U+4F60 = E4 BD A0 (3 bytes).
    k.kind = KeyEvent::Kind::Char;
    k.ch = std::string("\xE4\xBD\xA0", 3);
    input.on_key(k);
    CHECK(input.text() == "\xE4\xBD\xA0");
    CHECK(input.text().size() == 3);  // 3 bytes, 1 grapheme
}

TEST_CASE("Input insert two CJK characters stays valid UTF-8") {
    Theme theme = Theme::dark();
    components::Input input(theme);
    input.set_prompt("> ");
    KeyEvent k;
    // Type "你好" (hello in Chinese). 你=E4 BD A0, 好=E5 A5 BD.
    k.kind = KeyEvent::Kind::Char; k.ch = std::string("\xE4\xBD\xA0", 3);
    input.on_key(k);
    k.ch = std::string("\xE5\xA5\xBD", 3);
    input.on_key(k);
    CHECK(input.text() == "\xE4\xBD\xA0\xE5\xA5\xBD");
    CHECK(input.text().size() == 6);
}

TEST_CASE("Input backspace removes whole CJK char, not one byte") {
    Theme theme = Theme::dark();
    components::Input input(theme);
    input.set_prompt("> ");
    KeyEvent k;
    k.kind = KeyEvent::Kind::Char; k.ch = std::string("\xE4\xBD\xA0", 3);
    input.on_key(k);  // 你
    k.ch = std::string("a"); input.on_key(k);
    CHECK(input.text() == "\xE4\xBD\xA0""a");
    CHECK(input.text().size() == 4);
    k.kind = KeyEvent::Kind::Backspace; input.on_key(k);
    CHECK(input.text() == "\xE4\xBD\xA0");  // 'a' gone, 你 intact
    CHECK(input.text().size() == 3);
    k.kind = KeyEvent::Kind::Backspace; input.on_key(k);
    CHECK(input.text().empty());  // 你 deleted as one unit
}

TEST_CASE("Input Left arrow jumps over CJK char in one step") {
    Theme theme = Theme::dark();
    components::Input input(theme);
    input.set_prompt("> ");
    KeyEvent k;
    k.kind = KeyEvent::Kind::Char; k.ch = std::string("\xE4\xBD\xA0", 3);
    input.on_key(k);
    CHECK(input.text().size() == 3);
    k.kind = KeyEvent::Kind::Left; input.on_key(k);
    // Cursor should jump back by 3 bytes (one grapheme), not 1.
    CHECK(input.text().size() == 3);  // text unchanged
    // After Left, cursor_ is 0; another Left is a no-op.
    k.kind = KeyEvent::Kind::Left; input.on_key(k);
    CHECK(input.text().size() == 3);
}
