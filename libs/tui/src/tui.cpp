// libs/tui/src/tui.cpp
#include "pi_tui/tui.hpp"

#include "pi_core/ansi.hpp"

#include <sstream>

namespace pi::tui {

TUI::TUI(Terminal& term, Theme theme)
    : term_(term), theme_(std::move(theme)) {}

TUI::~TUI() = default;

void TUI::set_root(ComponentPtr root) {
    root_ = std::move(root);
}

void TUI::render() {
    if (!root_) return;
    auto [rows, cols] = term_.size();
    (void)rows;
    auto lines = root_->render(cols);
    // Pad lines to `cols` width with spaces, then append ANSI reset.
    std::string frame;
    frame.reserve(lines.size() * (cols + 16));
    for (auto& l : lines) {
        // Strip ANSI to count width.
        // Simple: assume l is at most cols; if longer, truncate visually.
        // For V1 we just append as-is.
        frame += l;
        frame += "\x1b[0m\r\n";
    }
    if (frame == prev_frame_) {
        return;  // no change
    }
    prev_frame_ = frame;
    // Repaint: move cursor home + clear + write.
    std::string out;
    out.reserve(frame.size() + 8);
    out += pi::core::ansi::clear_screen();
    out += pi::core::ansi::move_cursor(1, 1);
    out += frame;
    term_.write(out);
    term_.flush();
}

bool TUI::handle_key() {
    auto k = term_.try_read_key(50);
    if (!k) return true;
    if (root_) {
        if (root_->on_key(*k)) return true;
    }
    return k->kind != KeyEvent::Kind::CtrlD
        && k->kind != KeyEvent::Kind::CtrlC;
}

void TUI::run(std::function<bool()> should_quit,
              std::function<void(KeyEvent)> on_key) {
    while (!quit_ && (should_quit ? !should_quit() : true)) {
        render();
        auto k = term_.try_read_key(100);
        if (k) {
            if (on_key) on_key(*k);
            if (k->kind == KeyEvent::Kind::CtrlC || k->kind == KeyEvent::Kind::CtrlD) {
                quit_ = true;
            }
        }
    }
}

}  // namespace pi::tui
