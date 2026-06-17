// libs/tui/include/pi_tui/think_filter.hpp
// Stream-safe filter that splits text on <think>...</think> markers.
//
// Use case: providers like MiniMax / DeepSeek embed reasoning in plain
// text content rather than a dedicated thinking event. This filter lets
// the TUI colour the inside of the tags differently while stripping the
// tags themselves, even when a tag is split across delta chunks.

#pragma once

#include <string>

namespace pi::tui {

struct ThinkFilter {
    bool in_think = false;
    std::string buf;  // partial-tag accumulator

    // Feed one delta. Appends decorated output (tag-stripped, with
    // ANSI-coded reasoning regions) to `out`. `enter` is the escape
    // emitted when entering <think>; `leave` is the reset emitted on
    // </think>. Boundary-safe: a tag split across calls is preserved.
    void feed(const std::string& delta,
              const std::string& enter,
              const std::string& leave,
              std::string& out);
};

}  // namespace pi::tui
