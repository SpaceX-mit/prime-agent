// libs/tui/src/components/input.cpp
#include "pi_tui/components/input.hpp"
#include "pi_tui/terminal.hpp"

#include "pi_core/ansi.hpp"
#include "pi_core/strutil.hpp"

#include <algorithm>
#include <sstream>

namespace pi::tui::components {

Input::Input(Theme theme) : theme_(std::move(theme)) {}

void Input::set_prompt(std::string p) { prompt_ = std::move(p); }
void Input::set_text(std::string s) { text_ = std::move(s); cursor_ = text_.size(); }

bool Input::take_submit() {
    bool v = submit_pending_;
    submit_pending_ = false;
    return v;
}

void Input::push_history(std::string line) {
    if (line.empty()) return;
    if (!history_.empty() && history_.back() == line) return;
    history_.push_back(std::move(line));
    if (history_.size() > 200) history_.pop_front();
}

std::vector<std::string> Input::render(int width) const {
    (void)width;
    std::string line;
    line += theme_.input_bg;
    line += theme_.input_fg;
    line += prompt_;

    // Render text with an inverse-video cursor at `cursor_` so the user
    // can see where they are. Mirrors upstream pi's Editor render which
    // uses ANSI reverse-video (`\x1b[7m`) over the grapheme under the
    // cursor (or a trailing space if the cursor is at EOL).
    if (cursor_ <= text_.size()) {
        std::string before(text_, 0, cursor_);
        std::string after(text_, cursor_, std::string::npos);
        std::string cursor_glyph;
        std::string after_rest;
        if (!after.empty()) {
            // Find the first UTF-8 character in `after` and highlight it.
            size_t i = 0;
            // Walk past leading continuation bytes (shouldn't happen since
            // we always start at a char boundary, but be defensive).
            while (i < after.size() &&
                   ((unsigned char)after[i] & 0xC0) == 0x80)
                ++i;
            size_t char_end = i + 1;
            while (char_end < after.size() &&
                   ((unsigned char)after[char_end] & 0xC0) == 0x80)
                ++char_end;
            cursor_glyph = after.substr(i, char_end - i);
            after_rest = after.substr(char_end);
        } else {
            cursor_glyph = " ";
        }
        line += before;
        line += "\x1b[7m";   // reverse video
        line += cursor_glyph;
        line += "\x1b[0m";
        line += after_rest;
    } else {
        // Out-of-range cursor (shouldn't happen, but be safe).
        line += text_;
    }
    line += "\x1b[0m ";
    return {line};
}

bool Input::on_key(const KeyEvent& ev) {
    switch (ev.kind) {
        case KeyEvent::Kind::Char: {
            text_.insert(text_.begin() + static_cast<ptrdiff_t>(cursor_), ev.ch);
            cursor_++;
            history_idx_ = -1;
            return true;
        }
        case KeyEvent::Kind::Enter:
            submit_pending_ = true;
            return true;
        case KeyEvent::Kind::Backspace:
            if (cursor_ > 0) {
                text_.erase(text_.begin() + static_cast<ptrdiff_t>(cursor_) - 1);
                cursor_--;
            }
            return true;
        case KeyEvent::Kind::Left:
            if (cursor_ > 0) cursor_--;
            return true;
        case KeyEvent::Kind::Right:
            if (cursor_ < text_.size()) cursor_++;
            return true;
        case KeyEvent::Kind::Home:
            cursor_ = 0;
            return true;
        case KeyEvent::Kind::End:
            cursor_ = text_.size();
            return true;
        case KeyEvent::Kind::CtrlA:
            cursor_ = 0;
            return true;
        case KeyEvent::Kind::CtrlE:
            cursor_ = text_.size();
            return true;
        case KeyEvent::Kind::CtrlK:
            text_.erase(cursor_);
            return true;
        case KeyEvent::Kind::CtrlU:
            text_.erase(0, cursor_);
            cursor_ = 0;
            return true;
        case KeyEvent::Kind::CtrlW: {
            // Delete word backwards.
            size_t new_pos = cursor_;
            while (new_pos > 0 && std::isspace(static_cast<unsigned char>(text_[new_pos - 1])))
                --new_pos;
            while (new_pos > 0 && !std::isspace(static_cast<unsigned char>(text_[new_pos - 1])))
                --new_pos;
            text_.erase(new_pos, cursor_ - new_pos);
            cursor_ = new_pos;
            return true;
        }
        case KeyEvent::Kind::Up: {
            if (history_.empty()) return true;
            if (history_idx_ == -1) {
                saved_ = text_;
                history_idx_ = static_cast<int>(history_.size()) - 1;
            } else if (history_idx_ > 0) {
                --history_idx_;
            }
            text_ = history_[history_idx_];
            cursor_ = text_.size();
            return true;
        }
        case KeyEvent::Kind::Down: {
            if (history_idx_ == -1) return true;
            if (history_idx_ + 1 >= (int)history_.size()) {
                history_idx_ = -1;
                text_ = saved_;
            } else {
                ++history_idx_;
                text_ = history_[history_idx_];
            }
            cursor_ = text_.size();
            return true;
        }
        default:
            return false;
    }
}

}  // namespace pi::tui::components
