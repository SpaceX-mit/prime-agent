// libs/tui/include/pi_tui/message_render.hpp
// Faithful port of upstream pi's interactive message rendering
// (packages/coding-agent/src/modes/interactive/components/*). Each function
// returns a ready-to-emit block of lines (joined with '\n'), width-aware and
// aligned/padded to the terminal width exactly like upstream so the on-screen
// result matches: backgrounds span the full row, boxes have 1-col x / 1-row y
// padding, state drives tool background color, etc.
//
// These are pure string builders (no I/O), so they're unit-testable and the
// interactive loop just emits their output into the scroll region.
#pragma once

#include "pi_tui/theme.hpp"

#include <string>
#include <string_view>

namespace pi::tui::msg {

/// Tool execution visual state → background color selection.
enum class ToolState { Pending, Success, Error };

/// User prompt: Box(padX=1, padY=1) filled with userMessageBg, text in
/// userMessageText. Returns the full block (with leading/trailing bg padding
/// rows), each line padded to `width`. No trailing newline.
std::string user_message(std::string_view text, const Theme& t, int width);

/// Assistant body text: padding x=1, no background, `text` color. Wrapped to
/// width by the terminal (we do not hard-wrap; we only left-pad 1 col).
std::string assistant_text(std::string_view text, const Theme& t, int width);

/// Reasoning/thinking block: italic + thinkingText, padding x=1.
std::string thinking_text(std::string_view text, const Theme& t, int width);

/// Tool call announcement / result line. `title` is the tool name, `detail`
/// is an optional one-line preview (args or first output line). Rendered as a
/// Box(1,1) with the state background; title is bold toolTitle, detail is
/// toolOutput. Returns block padded to width.
std::string tool_execution(std::string_view title, std::string_view detail,
                           ToolState state, const Theme& t, int width);

/// One diff line. `raw` begins with '+', '-', or ' '. Colored with
/// toolDiffAdded / toolDiffRemoved / toolDiffContext respectively.
std::string diff_line(std::string_view raw, const Theme& t);

/// Compaction notice: Box(1,1) customMessageBg, bold [compaction] label in
/// customMessageLabel, then the summary in customMessageText.
std::string compaction_message(std::string_view summary, int dropped,
                               const Theme& t, int width);

}  // namespace pi::tui::msg
