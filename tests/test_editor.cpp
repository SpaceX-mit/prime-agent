// tests/test_editor.cpp
#define TEST_FRAMEWORK_IMPLEMENT
#include "test_framework.hpp"

#include "pi_tui/components/editor.hpp"
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
