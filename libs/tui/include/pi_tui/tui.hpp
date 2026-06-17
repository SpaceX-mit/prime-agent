// libs/tui/include/pi_tui/tui.hpp
// TUI: top-level container that renders a tree of components and diffs the
// previous frame to avoid flicker.

#pragma once

#include "pi_tui/component.hpp"
#include "pi_tui/terminal.hpp"
#include "pi_tui/theme.hpp"

#include <functional>
#include <memory>
#include <string>

namespace pi::tui {

class TUI {
public:
    TUI(Terminal& term, Theme theme);
    ~TUI();

    /// Set the root component.
    void set_root(ComponentPtr root);

    /// Render the next frame (diffed against the previous).
    void render();

    /// Read the next key event and dispatch to the focused component.
    /// Returns false on EOF/Ctrl-D.
    bool handle_key();

    /// Run the main loop until `should_quit()` returns true.
    void run(std::function<bool()> should_quit,
             std::function<void(KeyEvent)> on_key);

    /// Quit flag.
    void quit() { quit_ = true; }
    bool should_quit() const { return quit_; }

    Theme& theme() { return theme_; }
    const Theme& theme() const { return theme_; }
    Terminal& terminal() { return term_; }

private:
    Terminal& term_;
    Theme theme_;
    ComponentPtr root_;
    std::string prev_frame_;
    bool quit_ = false;
};

}  // namespace pi::tui
