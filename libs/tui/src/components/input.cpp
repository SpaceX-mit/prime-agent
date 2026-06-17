// libs/tui/src/components/input.cpp
#include "pi_tui/components/input.hpp"
#include "pi_tui/terminal.hpp"

#include "pi_core/ansi.hpp"
#include "pi_core/strutil.hpp"

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

// V3.9: sanitize UTF-8. Replaces orphan continuation bytes and
// overlong / out-of-range sequences with U+FFFD (\xEF\xBF\xBD) so
// downstream providers always receive valid UTF-8 in messages[].
// Walks the input once and produces a clean copy.
std::string sanitize_utf8(const std::string& s) {
    auto is_cont = [](unsigned char b) {
        return (b & 0xC0) == 0x80;
    };
    auto seq_len = [](unsigned char b) -> int {
        if (b < 0x80) return 1;
        if (b < 0xC0) return -1;
        if (b < 0xE0) return 2;
        if (b < 0xF0) return 3;
        if (b < 0xF8) return 4;
        return -1;
    };
    std::string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        unsigned char b = static_cast<unsigned char>(s[i]);
        int n = seq_len(b);
        if (n < 0) {
            out.append("\xEF\xBF\xBD");
            ++i;
            continue;
        }
        if (i + n > s.size()) {
            // truncated at end
            out.append("\xEF\xBF\xBD");
            break;
        }
        bool ok = true;
        for (int k = 1; k < n; ++k) {
            if (!is_cont(static_cast<unsigned char>(s[i + k]))) { ok = false; break; }
        }
        if (!ok) {
            out.append("\xEF\xBF\xBD");
            ++i;
            continue;
        }
        // Check for overlong encodings and surrogates.
        uint32_t cp = 0;
        if (n == 1) cp = b;
        else if (n == 2) cp = ((b & 0x1F) << 6)  | (s[i+1] & 0x3F);
        else if (n == 3) cp = ((b & 0x0F) << 12) | ((s[i+1] & 0x3F) << 6) | (s[i+2] & 0x3F);
        else               cp = ((b & 0x07) << 18) | ((s[i+1] & 0x3F) << 12)
                                | ((s[i+2] & 0x3F) << 6)  | (s[i+3] & 0x3F);
        bool overlong = (n == 2 && cp < 0x80) || (n == 3 && cp < 0x800) ||
                        (n == 4 && cp < 0x10000);
        bool surrogate = (cp >= 0xD800 && cp <= 0xDFFF);
        bool too_big = cp > 0x10FFFF;
        if (overlong || surrogate || too_big) {
            out.append("\xEF\xBF\xBD");
            i += n;
            continue;
        }
        out.append(s, i, n);
        i += n;
    }
    return out;
}

}  // namespace

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
            // ev.ch is now a full UTF-8 character (1-4 bytes), assembled
            // by Terminal::try_read_key(). Insert all bytes at once and
            // advance the cursor by the byte length so multi-byte CJK /
            // emoji characters don't get split.
            text_.insert(text_.begin() + static_cast<ptrdiff_t>(cursor_),
                         ev.ch.begin(), ev.ch.end());
            cursor_ += ev.ch.size();
            history_idx_ = -1;
            return true;
        }
        case KeyEvent::Kind::Paste: {
            // V3.7: terminal sent a paste event (bracketed paste). Insert
            // the entire pasted text at the cursor in one shot. V3.9 also
            // runs UTF-8 sanitization here so the buffer cannot become
            // invalid UTF-8 even if the pasted source was malformed.
            std::string clean = sanitize_utf8(ev.ch);
            text_.insert(text_.begin() + static_cast<ptrdiff_t>(cursor_),
                         clean.begin(), clean.end());
            cursor_ += clean.size();
            history_idx_ = -1;
            return true;
        }
        case KeyEvent::Kind::Enter:
            submit_pending_ = true;
            return true;
        case KeyEvent::Kind::Backspace:
            if (cursor_ > 0) {
                // Erase the previous UTF-8 character in one go so multi-byte
                // graphemes (CJK / emoji) don't get half-deleted.
                size_t prev = utf8_prev(text_, cursor_);
                text_.erase(text_.begin() + static_cast<ptrdiff_t>(prev),
                            text_.begin() + static_cast<ptrdiff_t>(cursor_));
                cursor_ = prev;
            }
            return true;
        case KeyEvent::Kind::Left:
            if (cursor_ > 0) cursor_ = utf8_prev(text_, cursor_);
            return true;
        case KeyEvent::Kind::Right:
            if (cursor_ < text_.size()) {
                size_t p = cursor_ + 1;
                while (p < text_.size() &&
                       ((unsigned char)text_[p] & 0xC0) == 0x80)
                    ++p;
                cursor_ = p;
            }
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
