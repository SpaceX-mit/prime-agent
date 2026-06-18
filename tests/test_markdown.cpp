// tests/test_markdown.cpp
// P2.6: Markdown → ANSI rendering. Verifies block + inline handling and that
// the upstream theme tokens are applied.
#define TEST_FRAMEWORK_IMPLEMENT
#include "test_framework.hpp"

#include "pi_tui/markdown_render.hpp"
#include "pi_tui/theme.hpp"

using namespace pi::tui;

static bool has(const std::string& h, const std::string& n) {
    return h.find(n) != std::string::npos;
}

TEST_CASE("inline bold/italic/code/strikethrough") {
    Theme t = Theme::dark();
    CHECK(has(md::render_inline("a **b** c", t), "\x1b[1m"));     // bold on
    CHECK(has(md::render_inline("a **b** c", t), "\x1b[22m"));    // bold off
    CHECK(has(md::render_inline("a *b* c", t), "\x1b[3m"));       // italic on
    CHECK(has(md::render_inline("a `code` c", t), t.md_code));    // code color
    CHECK(has(md::render_inline("x ~~y~~ z", t), "\x1b[9m"));     // strike on
}

TEST_CASE("inline link renders text + url") {
    Theme t = Theme::dark();
    auto s = md::render_inline("see [pi](https://x.y)", t);
    CHECK(has(s, t.md_link));
    CHECK(has(s, "\x1b[4m"));            // underline
    CHECK(has(s, "https://x.y"));        // url shown when != label
}

TEST_CASE("heading uses md_heading + bold; H1 underlined") {
    Theme t = Theme::dark();
    auto h1 = md::render("# Title", t, 40);
    CHECK(has(h1, t.md_heading));
    CHECK(has(h1, "\x1b[1m"));
    CHECK(has(h1, "\x1b[4m"));   // H1 underline
    auto h3 = md::render("### Sub", t, 40);
    CHECK(has(h3, "### "));      // level>=3 keeps the # prefix
}

TEST_CASE("fenced code block styled with md_code_block") {
    Theme t = Theme::dark();
    auto s = md::render("```c\nint x;\n```", t, 40);
    CHECK(has(s, t.md_code_block));
    CHECK(has(s, "int x;"));
}

TEST_CASE("unordered + ordered lists use md_list_bullet") {
    Theme t = Theme::dark();
    auto ul = md::render("- one\n- two", t, 40);
    CHECK(has(ul, t.md_list_bullet));
    CHECK(has(ul, "\xE2\x80\xA2"));   // • bullet
    auto ol = md::render("1. first\n2. second", t, 40);
    CHECK(has(ol, t.md_list_bullet));
    CHECK(has(ol, "1. "));
}

TEST_CASE("blockquote and hr") {
    Theme t = Theme::dark();
    auto q = md::render("> quoted", t, 20);
    CHECK(has(q, "\xE2\x96\x8E"));    // ▎ quote bar
    auto hr = md::render("---", t, 10);
    CHECK(has(hr, "----------"));     // 10-wide rule
}

TEST_CASE("plain code fence does not bleed into following text") {
    Theme t = Theme::dark();
    auto s = md::render("```\ncode\n```\nafter", t, 40);
    CHECK(has(s, "after"));
}
