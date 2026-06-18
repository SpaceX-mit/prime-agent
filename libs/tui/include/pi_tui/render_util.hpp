// libs/tui/include/pi_tui/render_util.hpp
// Width-aware text helpers mirroring upstream pi's packages/tui/src/utils.ts
// (visibleWidth, truncateToWidth, padding, applyBackgroundToLine). Used by
// the message renderers and footer so on-screen alignment matches upstream.
#pragma once

#include <string>
#include <string_view>

namespace pi::tui::render {

/// Visible column width of a string, ignoring ANSI/OSC escape sequences and
/// counting CJK/wide characters as 2 (via pi_core unicode display_width).
/// Mirrors utils.ts visibleWidth().
int visible_width(std::string_view s);

/// Truncate `s` to at most `max_width` visible columns, appending `ellipsis`
/// (already-styled allowed) if truncation occurs. ANSI sequences are passed
/// through and never counted. Mirrors utils.ts truncateToWidth().
std::string truncate_to_width(std::string_view s, int max_width,
                              std::string_view ellipsis = "...");

/// Right-pad `s` with spaces so its visible width reaches `width`
/// (no-op if already >= width).
std::string pad_to_width(std::string_view s, int width);

/// Pad `line` to `width` columns then wrap the whole padded line in `bg`
/// (a background SGR string) + reset. Background therefore extends across
/// the full row, matching upstream applyBackgroundToLine().
std::string apply_bg_to_line(std::string_view line, int width, std::string_view bg);

}  // namespace pi::tui::render
