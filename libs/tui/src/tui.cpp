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
    // Per-row diff: clear+rewrite only lines that changed since the last
    // frame. Avoids the full clear_screen() flicker that the previous
    // implementation caused on every keystroke / streaming delta.
    std::string out;
    out.reserve(64);
    out += pi::core::ansi::hide_cursor();

    bool full_repaint = (cols != prev_cols_) ||
                        (lines.size() != prev_lines_.size());
    if (full_repaint) {
        out += pi::core::ansi::clear_screen();
        out += pi::core::ansi::move_cursor(1, 1);
        for (size_t i = 0; i < lines.size(); ++i) {
            out += lines[i];
            out += "\x1b[0m";
            if (i + 1 < lines.size()) out += "\r\n";
        }
    } else {
        for (size_t i = 0; i < lines.size(); ++i) {
            if (lines[i] == prev_lines_[i]) continue;
            out += pi::core::ansi::move_cursor(static_cast<int>(i) + 1, 1);
            out += pi::core::ansi::clear_line();
            out += lines[i];
            out += "\x1b[0m";
        }
    }
    out += pi::core::ansi::show_cursor();
    if (out.size() > pi::core::ansi::hide_cursor().size() +
                     pi::core::ansi::show_cursor().size()) {
        term_.write(out);
        term_.flush();
    }
    prev_lines_ = std::move(lines);
    prev_cols_ = cols;
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
