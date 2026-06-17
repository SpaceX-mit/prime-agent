// tests/test_tui.cpp
#define TEST_FRAMEWORK_IMPLEMENT
#include "test_framework.hpp"

#include "pi_tui/components/box.hpp"
#include "pi_tui/components/footer.hpp"
#include "pi_tui/components/input.hpp"
#include "pi_tui/components/text.hpp"
#include "pi_tui/terminal.hpp"
#include "pi_tui/theme.hpp"

#include <string>
#include <vector>

using namespace pi;
using namespace pi::tui;

TEST_CASE("Text renders single line") {
    components::Text t("hello");
    auto lines = t.render(80);
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].find("hello") != std::string::npos);
}

TEST_CASE("Text splits on newlines") {
    components::Text t("a\nb\nc");
    auto lines = t.render(80);
    REQUIRE(lines.size() == 3);
    CHECK(lines[0].find("a") != std::string::npos);
    CHECK(lines[1].find("b") != std::string::npos);
    CHECK(lines[2].find("c") != std::string::npos);
}

TEST_CASE("Box vertical stacks") {
    components::Box box(components::Box::Vertical, 0);
    box.add(std::make_shared<components::Text>("a"));
    box.add(std::make_shared<components::Text>("b"));
    auto lines = box.render(80);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0].find("a") != std::string::npos);
    CHECK(lines[1].find("b") != std::string::npos);
}

TEST_CASE("Input accepts text input") {
    Theme theme = Theme::dark();
    components::Input input(theme);
    input.set_prompt("> ");
    KeyEvent k;
    k.kind = KeyEvent::Kind::Char;
    k.ch = 'h';
    CHECK(input.on_key(k));
    k.ch = 'i';
    CHECK(input.on_key(k));
    CHECK(input.text() == "hi");
}

TEST_CASE("Input backspace") {
    Theme theme = Theme::dark();
    components::Input input(theme);
    KeyEvent k;
    k.kind = KeyEvent::Kind::Char; k.ch = 'a'; input.on_key(k);
    k.ch = 'b'; input.on_key(k);
    k.kind = KeyEvent::Kind::Backspace; input.on_key(k);
    CHECK(input.text() == "a");
}

TEST_CASE("Input enter sets submit") {
    Theme theme = Theme::dark();
    components::Input input(theme);
    KeyEvent k;
    k.kind = KeyEvent::Kind::Char; k.ch = 'x'; input.on_key(k);
    k.kind = KeyEvent::Kind::Enter; input.on_key(k);
    CHECK(input.take_submit());
    CHECK_FALSE(input.take_submit());
}

TEST_CASE("Input history navigation") {
    Theme theme = Theme::dark();
    components::Input input(theme);
    input.push_history("first");
    input.push_history("second");
    KeyEvent k;
    k.kind = KeyEvent::Kind::Char; k.ch = 'n'; input.on_key(k);
    k.kind = KeyEvent::Kind::Up; input.on_key(k);
    CHECK(input.text() == "second");
    k.kind = KeyEvent::Kind::Up; input.on_key(k);
    CHECK(input.text() == "first");
    k.kind = KeyEvent::Kind::Down; input.on_key(k);
    CHECK(input.text() == "second");
}

TEST_CASE("Footer renders with mode and model") {
    Theme theme = Theme::dark();
    components::Footer footer(theme, "interactive", "claude-sonnet-4-5");
    auto lines = footer.render(80);
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].find("interactive") != std::string::npos);
    CHECK(lines[0].find("claude-sonnet-4-5") != std::string::npos);
}
