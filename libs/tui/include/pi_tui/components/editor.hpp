// libs/tui/include/pi_tui/components/editor.hpp
#pragma once

#include "pi_tui/component.hpp"
#include "pi_tui/terminal.hpp"
#include "pi_tui/theme.hpp"

#include <deque>
#include <string>
#include <vector>

namespace pi::tui::components {

/// Multi-line text editor with history and Ctrl-J submit.
/// - Enter        : newline
/// - Ctrl-J / Ctrl-Enter : submit
/// - Ctrl-C       : cancel submit (set cancel_pending)
/// - Up/Down      : history navigation
/// - Left/Right   : cursor movement (within line)
/// - Home/End     : line start / end
/// - Backspace    : delete char before cursor
/// - Delete       : delete char at cursor
/// - Ctrl-K       : kill to end of line
/// - Ctrl-U       : kill to start of line
/// - Ctrl-W       : kill word before cursor
class Editor : public Component {
public:
    explicit Editor(Theme theme);

    void set_prompt(std::string p);
    void set_placeholder(std::string s);

    /// True if Ctrl-J was pressed since last `take_submit()`.
    bool submit_pending() const { return submit_pending_; }
    bool take_submit();

    /// True if Ctrl-C was pressed (cancels without submitting).
    bool cancel_pending() const { return cancel_pending_; }
    bool take_cancel();

    const std::string& text() const { return text_; }
    void set_text(std::string s);

    /// History (caller pushes after submit).
    void push_history(std::string entry);
    void clear_history();

    std::vector<std::string> render(int width) const override;
    bool on_key(const KeyEvent& ev) override;

private:
    Theme theme_;
    std::string prompt_;
    std::string placeholder_;
    std::string text_;             // text with '\n' as line separator
    size_t cursor_ = 0;            // byte offset into text_
    std::deque<std::string> history_;
    int history_idx_ = -1;
    std::string saved_;
    bool submit_pending_ = false;
    bool cancel_pending_ = false;

    void insert_char(char c);
    void insert_str(std::string s);
    void delete_back();
    void delete_forward();
    void kill_to_eol();
    void kill_to_sol();
    void kill_word_back();

    /// Cursor helpers (byte offsets into text_).
    size_t line_start(size_t pos) const;
    size_t line_end(size_t pos) const;
    size_t prev_char(size_t pos) const;
    size_t next_char(size_t pos) const;
    size_t prev_line(size_t pos) const;
    size_t next_line(size_t pos) const;
};

}  // namespace pi::tui::components
