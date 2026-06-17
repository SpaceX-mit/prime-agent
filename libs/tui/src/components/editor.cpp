// libs/tui/src/components/editor.cpp
#include "pi_tui/components/editor.hpp"
#include "pi_tui/terminal.hpp"

#include "pi_core/ansi.hpp"

#include <algorithm>
#include <sstream>

namespace pi::tui::components {

namespace {

// Walk back over UTF-8 bytes to find the previous character start.
size_t utf8_prev(const std::string& s, size_t pos) {
    if (pos == 0) return 0;
    size_t p = pos;
    do {
        --p;
    } while (p > 0 && ((unsigned char)s[p] & 0xC0) == 0x80);
    return p;
}
size_t utf8_next(const std::string& s, size_t pos) {
    if (pos >= s.size()) return s.size();
    size_t p = pos + 1;
    while (p < s.size() && ((unsigned char)s[p] & 0xC0) == 0x80) ++p;
    return p;
}

}  // namespace

Editor::Editor(Theme theme) : theme_(std::move(theme)) {}

void Editor::set_prompt(std::string p) { prompt_ = std::move(p); }
void Editor::set_placeholder(std::string s) { placeholder_ = std::move(s); }
void Editor::set_text(std::string s) { text_ = std::move(s); cursor_ = text_.size(); }

bool Editor::take_submit() {
    bool v = submit_pending_;
    submit_pending_ = false;
    return v;
}
bool Editor::take_cancel() {
    bool v = cancel_pending_;
    cancel_pending_ = false;
    return v;
}

void Editor::push_history(std::string entry) {
    if (entry.empty()) return;
    if (!history_.empty() && history_.back() == entry) return;
    history_.push_back(std::move(entry));
    if (history_.size() > 200) history_.pop_front();
}
void Editor::clear_history() {
    history_.clear();
    history_idx_ = -1;
    saved_.clear();
}

void Editor::insert_char(char c) {
    text_.insert(text_.begin() + (long)cursor_, c);
    cursor_++;
}
void Editor::insert_str(std::string s) {
    text_.insert(text_.begin() + (long)cursor_, s.begin(), s.end());
    cursor_ += s.size();
}
void Editor::delete_back() {
    if (cursor_ == 0) return;
    size_t prev = utf8_prev(text_, cursor_);
    text_.erase(text_.begin() + (long)prev, text_.begin() + (long)cursor_);
    cursor_ = prev;
}
void Editor::delete_forward() {
    if (cursor_ >= text_.size()) return;
    size_t nxt = utf8_next(text_, cursor_);
    text_.erase(text_.begin() + (long)cursor_, text_.begin() + (long)nxt);
}
void Editor::kill_to_eol() {
    size_t eol = line_end(cursor_);
    text_.erase(text_.begin() + (long)cursor_, text_.begin() + (long)eol);
}
void Editor::kill_to_sol() {
    size_t sol = line_start(cursor_);
    text_.erase(text_.begin() + (long)sol, text_.begin() + (long)cursor_);
    cursor_ = sol;
}
void Editor::kill_word_back() {
    size_t p = cursor_;
    // Skip trailing spaces of the word we're deleting.
    while (p > 0 && std::isspace((unsigned char)text_[p - 1])) --p;
    while (p > 0 && !std::isspace((unsigned char)text_[p - 1])) --p;
    text_.erase(text_.begin() + (long)p, text_.begin() + (long)cursor_);
    cursor_ = p;
}

size_t Editor::line_start(size_t pos) const {
    while (pos > 0 && text_[pos - 1] != '\n') --pos;
    return pos;
}
size_t Editor::line_end(size_t pos) const {
    while (pos < text_.size() && text_[pos] != '\n') ++pos;
    return pos;
}
size_t Editor::prev_char(size_t pos) const { return utf8_prev(text_, pos); }
size_t Editor::next_char(size_t pos) const { return utf8_next(text_, pos); }
size_t Editor::prev_line(size_t pos) const {
    size_t sol = line_start(pos);
    if (sol == 0) return 0;
    size_t prev_sol = line_start(sol - 1);
    size_t col = pos - sol;
    size_t prev_eol = line_end(prev_sol);
    return prev_sol + std::min(col, prev_eol - prev_sol);
}
size_t Editor::next_line(size_t pos) const {
    size_t eol = line_end(pos);
    if (eol >= text_.size()) return text_.size();
    size_t nxt_sol = eol + 1;
    size_t nxt_eol = line_end(nxt_sol);
    size_t col = pos - line_start(pos);
    return nxt_sol + std::min(col, nxt_eol - nxt_sol);
}

std::vector<std::string> Editor::render(int width) const {
    (void)width;
    if (text_.empty()) {
        return {prompt_ + theme_.dim + placeholder_ + "\x1b[0m"};
    }
    std::vector<std::string> lines;
    size_t pos = 0;
    bool first = true;
    while (pos <= text_.size()) {
        size_t eol = line_end(pos);
        std::string line;
        if (first) {
            line += prompt_;
            first = false;
        } else {
            line += theme_.dim + "... " + "\x1b[0m";
        }
        line += theme_.input_fg;
        line.append(text_, pos, eol - pos);
        line += "\x1b[0m";
        lines.push_back(line);
        if (eol >= text_.size()) break;
        pos = eol + 1;
    }
    return lines;
}

bool Editor::on_key(const KeyEvent& ev) {
    switch (ev.kind) {
        case KeyEvent::Kind::Char:
            insert_char(ev.ch);
            history_idx_ = -1;
            return true;
        case KeyEvent::Kind::Enter:
            insert_char('\n');
            history_idx_ = -1;
            return true;
        case KeyEvent::Kind::Backspace:
            delete_back();
            history_idx_ = -1;
            return true;
        case KeyEvent::Kind::Delete:
            delete_forward();
            history_idx_ = -1;
            return true;
        case KeyEvent::Kind::Left:
            if (cursor_ > 0) cursor_ = prev_char(cursor_);
            return true;
        case KeyEvent::Kind::Right:
            if (cursor_ < text_.size()) cursor_ = next_char(cursor_);
            return true;
        case KeyEvent::Kind::Up:
            if (!history_.empty()) {
                if (history_idx_ == -1) {
                    saved_ = text_;
                    history_idx_ = (int)history_.size() - 1;
                } else if (history_idx_ > 0) {
                    --history_idx_;
                }
                text_ = history_[history_idx_];
                cursor_ = text_.size();
            } else {
                cursor_ = prev_line(cursor_);
            }
            return true;
        case KeyEvent::Kind::Down:
            if (history_idx_ == -1) {
                cursor_ = next_line(cursor_);
            } else if (history_idx_ + 1 >= (int)history_.size()) {
                history_idx_ = -1;
                text_ = saved_;
                cursor_ = text_.size();
            } else {
                ++history_idx_;
                text_ = history_[history_idx_];
                cursor_ = text_.size();
            }
            return true;
        case KeyEvent::Kind::Home:
            cursor_ = line_start(cursor_);
            return true;
        case KeyEvent::Kind::End:
            cursor_ = line_end(cursor_);
            return true;
        case KeyEvent::Kind::CtrlA:
            cursor_ = line_start(cursor_);
            return true;
        case KeyEvent::Kind::CtrlE:
            cursor_ = line_end(cursor_);
            return true;
        case KeyEvent::Kind::CtrlK:
            kill_to_eol();
            return true;
        case KeyEvent::Kind::CtrlU:
            kill_to_sol();
            return true;
        case KeyEvent::Kind::CtrlW:
            kill_word_back();
            return true;
        case KeyEvent::Kind::CtrlJ:
        case KeyEvent::Kind::CtrlM:
            // Submit.
            submit_pending_ = true;
            return true;
        case KeyEvent::Kind::CtrlC:
            cancel_pending_ = true;
            return true;
        default:
            return false;
    }
}

}  // namespace pi::tui::components
