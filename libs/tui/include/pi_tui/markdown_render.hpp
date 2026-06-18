// libs/tui/include/pi_tui/markdown_render.hpp
// Line-oriented Markdown → ANSI renderer. A pragmatic faithful port of
// upstream pi's markdown component (packages/tui/src/components/markdown.ts):
// it covers the blocks and inline spans that show up in assistant output
// and styles them with the same theme tokens (mdHeading, mdCode,
// mdCodeBlock, mdListBullet, etc.). Not a full CommonMark parser — it is a
// single-pass line scanner, which is enough for terminal display and keeps
// the dependency surface at zero.
#pragma once

#include "pi_tui/theme.hpp"

#include <string>
#include <string_view>

namespace pi::tui::md {

/// Render a Markdown document to an ANSI-styled block (lines joined by '\n',
/// no trailing newline). `width` is used for horizontal rules.
/// Supported blocks: ATX headings (#..######), fenced code (``` / ~~~),
/// blockquotes (>), unordered (-,*,+) and ordered (1.) lists, horizontal
/// rules (---, ***, ___). Inline: **bold**, *em*/_em_, `code`, ~~del~~,
/// [text](url). Everything else passes through as styled paragraph text.
std::string render(std::string_view markdown, const Theme& t, int width);

/// Render a single inline string (no block handling). Exposed for reuse and
/// testing.
std::string render_inline(std::string_view text, const Theme& t);

}  // namespace pi::tui::md
