// libs/tui/src/terminal.cpp
// POSIX raw-mode terminal using termios.

#include "pi_tui/terminal.hpp"
#include "pi_core/ansi.hpp"
#include "pi_core/log.hpp"

#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#include <atomic>
#include <cstring>

namespace pi::tui {

namespace {

std::atomic<struct termios*> g_orig_termios{nullptr};
std::atomic<bool> g_signal_installed{false};

void sigwinch_handler(int /*sig*/) {
    // No-op; main loop polls size() each frame.
}

void reset_terminal_quickly() {
    auto* orig = g_orig_termios.load();
    if (orig) {
        ::tcsetattr(STDIN_FILENO, TCSAFLUSH, orig);
    }
    // Also show cursor + clear styling.
    ::write(STDOUT_FILENO, "\x1b[?25h\x1b[0m", 8);
    ::write(STDOUT_FILENO, "\x1b[?1049l", 8);  // leave alt screen
}

void install_signal_handler_once() {
    if (g_signal_installed.exchange(true)) return;
    struct sigaction sa{};
    sa.sa_handler = sigwinch_handler;
    ::sigemptyset(&sa.sa_mask);
    ::sigaction(SIGWINCH, &sa, nullptr);
    // Make sure SIGINT/SIGTERM go to default behavior (immediate process exit).
}

KeyEvent parse_escape_sequence(std::string_view seq) {
    KeyEvent ev;
    if (seq.empty()) { ev.kind = KeyEvent::Kind::Escape; return ev; }
    if (seq == "[A") { ev.kind = KeyEvent::Kind::Up; return ev; }
    if (seq == "[B") { ev.kind = KeyEvent::Kind::Down; return ev; }
    if (seq == "[C") { ev.kind = KeyEvent::Kind::Right; return ev; }
    if (seq == "[D") { ev.kind = KeyEvent::Kind::Left; return ev; }
    if (seq == "[H") { ev.kind = KeyEvent::Kind::Home; return ev; }
    if (seq == "[F") { ev.kind = KeyEvent::Kind::End; return ev; }
    if (seq == "[3~") { ev.kind = KeyEvent::Kind::Delete; return ev; }
    if (seq == "[5~") { ev.kind = KeyEvent::Kind::PageUp; return ev; }
    if (seq == "[6~") { ev.kind = KeyEvent::Kind::PageDown; return ev; }
    if (seq.size() == 3 && seq[0] == '[' && seq[1] == '1' && seq[2] >= '0' && seq[2] <= '5') {
        // F1-F5
    }
    if (seq.size() >= 3 && seq[0] == '[' && seq[1] == '1' && seq[2] >= '0' && seq[2] <= '5') {
        // F1-F5
        static const KeyEvent::Kind kF[] = {
            KeyEvent::Kind::F1, KeyEvent::Kind::F2, KeyEvent::Kind::F3,
            KeyEvent::Kind::F4, KeyEvent::Kind::F5,
        };
        ev.kind = kF[seq[2] - '0'];
        return ev;
    }
    if (seq.size() >= 3 && seq[0] == '[' && seq[1] == '1' && seq[2] >= '7' && seq[2] <= '9') {
        static const KeyEvent::Kind kF[] = {
            KeyEvent::Kind::F6, KeyEvent::Kind::F7, KeyEvent::Kind::F8,
        };
        ev.kind = kF[seq[2] - '7'];
        return ev;
    }
    // Generic CSI ; ... ~ form e.g. [1;5A (Ctrl-Up)
    if (seq.size() >= 4 && seq[0] == '[' && seq[2] == ';' && seq[3] == '5') {
        // Ctrl-modified
        char last = seq.back();
        switch (last) {
            case 'A': ev.kind = KeyEvent::Kind::Up; return ev;
            case 'B': ev.kind = KeyEvent::Kind::Down; return ev;
            case 'C': ev.kind = KeyEvent::Kind::Right; return ev;
            case 'D': ev.kind = KeyEvent::Kind::Left; return ev;
        }
    }
    ev.kind = KeyEvent::Kind::Unknown;
    ev.raw = std::string(seq);
    return ev;
}

KeyEvent classify(std::string_view utf8) {
    KeyEvent ev;
    if (utf8.empty()) { ev.kind = KeyEvent::Kind::Unknown; return ev; }
    unsigned char c = static_cast<unsigned char>(utf8[0]);
    if (utf8.size() == 1) {
        if (c == '\r' || c == '\n') { ev.kind = KeyEvent::Kind::Enter; return ev; }
        if (c == 0x7F || c == 0x08) { ev.kind = KeyEvent::Kind::Backspace; return ev; }
        if (c == 0x1B) { ev.kind = KeyEvent::Kind::Escape; return ev; }
        if (c == 0x09) { ev.kind = KeyEvent::Kind::Tab; return ev; }
        if (c >= 1 && c <= 26) {
            // Ctrl-A..Ctrl-Z
            static const KeyEvent::Kind kCtrl[] = {
                KeyEvent::Kind::CtrlA, KeyEvent::Kind::CtrlB, KeyEvent::Kind::CtrlC,
                KeyEvent::Kind::CtrlD, KeyEvent::Kind::CtrlE, KeyEvent::Kind::CtrlF,
                KeyEvent::Kind::CtrlG, KeyEvent::Kind::CtrlH, KeyEvent::Kind::CtrlI,
                KeyEvent::Kind::CtrlJ, KeyEvent::Kind::CtrlK, KeyEvent::Kind::CtrlL,
                KeyEvent::Kind::CtrlM, KeyEvent::Kind::CtrlN, KeyEvent::Kind::CtrlO,
                KeyEvent::Kind::CtrlP, KeyEvent::Kind::CtrlQ, KeyEvent::Kind::CtrlR,
                KeyEvent::Kind::CtrlS, KeyEvent::Kind::CtrlT, KeyEvent::Kind::CtrlU,
                KeyEvent::Kind::CtrlV, KeyEvent::Kind::CtrlW, KeyEvent::Kind::CtrlX,
                KeyEvent::Kind::CtrlY, KeyEvent::Kind::CtrlZ,
            };
            ev.kind = kCtrl[c - 1];
            return ev;
        }
    }
    // Default: printable character. For multi-byte UTF-8 the full string
    // is preserved as `ev.ch` so the input layer can insert it as a
    // single grapheme.
    ev.kind = KeyEvent::Kind::Char;
    ev.ch = std::string(utf8);
    return ev;
}

}  // namespace

Terminal::Terminal() = default;
Terminal::~Terminal() { leave_raw_mode(); }

void Terminal::enter_raw_mode() {
    if (raw_) return;
    if (!is_tty()) return;
    auto* orig = new struct termios;
    if (::tcgetattr(STDIN_FILENO, orig) != 0) {
        delete orig;
        return;
    }
    auto raw = *orig;
    raw.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= CS8;
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    if (::tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
        delete orig;
        return;
    }
    g_orig_termios.store(orig);
    install_signal_handler_once();
    raw_ = true;
    write(pi::core::ansi::enter_alt_screen());
    write(pi::core::ansi::hide_cursor());
    write(pi::core::ansi::clear_screen());
    // V3.7: enable bracketed paste mode and Kitty keyboard protocol
    // (disambiguate flag). Most modern terminals respond to these by
    // wrapping pastes in ESC[200~...ESC[201~ and sending individual keys
    // as CSI-u sequences with full Unicode codepoints.
    write("\x1b[?2004h");  // bracketed paste mode
    write("\x1b[>1u");     // Kitty disambiguate escape codes
    bracketed_paste_active_ = true;
    flush();
}

void Terminal::enable_bracketed_paste_mode() {
    write("\x1b[?2004h");
    flush();
    bracketed_paste_active_ = true;
}

void Terminal::disable_bracketed_paste_mode() {
    write("\x1b[?2004l");
    flush();
    bracketed_paste_active_ = false;
}

void Terminal::leave_raw_mode() {
    if (!raw_) return;
    write(pi::core::ansi::show_cursor());
    write(pi::core::ansi::RESET);
    write(pi::core::ansi::exit_alt_screen());
    flush();
    auto* orig = g_orig_termios.exchange(nullptr);
    if (orig) {
        ::tcsetattr(STDIN_FILENO, TCSAFLUSH, orig);
        delete orig;
    }
    raw_ = false;
}

std::pair<int, int> Terminal::size() const {
    struct winsize ws{};
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        return {ws.ws_row, ws.ws_col};
    }
    return {24, 80};
}

KeyEvent Terminal::read_key() {
    auto k = try_read_key(-1);
    while (!k) k = try_read_key(50);
    return *k;
}

std::optional<KeyEvent> Terminal::try_read_key(int timeout_ms) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);
    struct timeval tv{};
    if (timeout_ms >= 0) {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
    }
    int r = ::select(STDIN_FILENO + 1, &rfds, nullptr, nullptr,
                     timeout_ms < 0 ? nullptr : &tv);
    if (r <= 0) return std::nullopt;

    // V3.8: prepend any leftover bytes from a previous read that ended
    // mid-UTF-8-sequence. After this, "working bytes" is the sequence
    // we will classify this iteration.
    std::string work = utf8_pending_;
    utf8_pending_.clear();

    // V3.7: detect paste-start sequence (ESC [ 200 ~). If we see it,
    // accumulate bytes until paste-end (ESC [ 201 ~) and return as a
    // single Paste event.
    auto starts_with_esc = [&](const std::string& s) {
        return s.size() >= 1 && (unsigned char)s[0] == 0x1B;
    };
    auto consume_byte = [&]() -> int {
        if (!work.empty()) {
            int b = (unsigned char)work[0];
            work.erase(work.begin());
            return b;
        }
        unsigned char c;
        ssize_t n = ::read(STDIN_FILENO, &c, 1);
        if (n != 1) return -1;
        return (int)c;
    };

    int first = consume_byte();
    if (first < 0) return std::nullopt;

    // ----------------------------------------------------------------
    // V3.7: bracketed paste start detection.
    // The terminal sends ESC [ 2 0 0 ~ as a single 6-byte sequence
    // (1B 5B 32 30 30 7E) when paste begins, and ESC [ 2 0 1 ~ at the
    // end. We accumulate everything in between and emit it as one
    // Paste event.
    // ----------------------------------------------------------------
    if (first == 0x1B) {
        // Peek the next byte; if it's '[', maybe an escape sequence.
        fd_set rf;
        FD_ZERO(&rf); FD_SET(STDIN_FILENO, &rf);
        struct timeval t{0, 30'000};
        if (::select(STDIN_FILENO + 1, &rf, nullptr, nullptr, &t) > 0) {
            unsigned char c2;
            if (::read(STDIN_FILENO, &c2, 1) == 1) {
                if (c2 == '[' || c2 == 'O') {
                    std::string seq; seq.push_back(static_cast<char>(c2));
                    while (true) {
                        fd_set rf3; FD_ZERO(&rf3); FD_SET(STDIN_FILENO, &rf3);
                        struct timeval t3{0, 30'000};
                        if (::select(STDIN_FILENO + 1, &rf3, nullptr, nullptr, &t3) <= 0) break;
                        unsigned char c3;
                        if (::read(STDIN_FILENO, &c3, 1) != 1) break;
                        seq.push_back(static_cast<char>(c3));
                        if (c3 >= 0x40 && c3 <= 0x7E) break;  // final byte
                    }

                    // Bracketed paste: ESC [ 200 ~ content ESC [ 201 ~
                    if (seq == "[200~") {
                        std::string pasted;
                        // Read until we see ESC [ 201 ~.
                        std::string esc_buf;
                        while (true) {
                            int b = consume_byte();
                            if (b < 0) break;
                            if (b == 0x1B) {
                                esc_buf.push_back((char)b);
                                continue;
                            }
                            if (!esc_buf.empty()) {
                                esc_buf.push_back((char)b);
                                if (esc_buf == "\x1b[201~") break;
                                // Not the paste-end marker; flush.
                                pasted += esc_buf;
                                esc_buf.clear();
                                continue;
                            }
                            pasted.push_back((char)b);
                        }
                        KeyEvent ev;
                        ev.kind = KeyEvent::Kind::Paste;
                        ev.ch = pasted;  // full UTF-8 content
                        return ev;
                    }

                    // Kitty keyboard protocol: ESC [ <codepoint> [;<mod>] [:<event>] u
                    // Disambiguate escape codes (flag 1) sends every key
                    // as a CSI-u sequence. Format example:
                    //   ESC [ 97 ; 5 u       → Ctrl-A
                    //   ESC [ 22996 u        → CJK ideograph (single codepoint!)
                    //   ESC [ 127 ; 5 : 1 u  → Ctrl-Del release
                    // We accept the printable form (no Ctrl/Alt, codepoint
                    // >= 32) and emit a single Char with the UTF-8 of that
                    // codepoint — bypassing the multi-byte assembly entirely.
                    if (seq.size() >= 3 && seq.back() == 'u' &&
                        seq[0] == '[' &&
                        (seq[1] >= '0' && seq[1] <= '9')) {
                        // Parse <codepoint>[:<shifted>][;<mod>][:<event>]
                        std::string body(seq.begin() + 1, seq.end() - 1);
                        auto colon1 = body.find(':');
                        auto semi1 = body.find(';');
                        std::string cp_str = body.substr(
                            0, std::min(colon1 == std::string::npos ? body.size() : colon1,
                                        semi1 == std::string::npos ? body.size() : semi1));
                        int mod = 1;
                        if (semi1 != std::string::npos) {
                            std::string mod_str = body.substr(semi1 + 1);
                            auto colon2 = mod_str.find(':');
                            if (colon2 != std::string::npos) mod_str = mod_str.substr(0, colon2);
                            try { mod = std::stoi(mod_str); } catch (...) { mod = 1; }
                        }
                        // mod is 1-indexed (1 = no modifiers); subtract 1.
                        int mod_bits = mod > 0 ? mod - 1 : 0;
                        const int SHIFT = 1, ALT = 2, CTRL = 4;
                        if ((mod_bits & (ALT | CTRL)) == 0) {
                            try {
                                int cp = std::stoi(cp_str);
                                if (cp >= 32 && cp <= 0x10FFFF) {
                                    // Encode codepoint as UTF-8.
                                    std::string utf8_cp;
                                    if (cp < 0x80) {
                                        utf8_cp.push_back((char)cp);
                                    } else if (cp < 0x800) {
                                        utf8_cp.push_back((char)(0xC0 | (cp >> 6)));
                                        utf8_cp.push_back((char)(0x80 | (cp & 0x3F)));
                                    } else if (cp < 0x10000) {
                                        utf8_cp.push_back((char)(0xE0 | (cp >> 12)));
                                        utf8_cp.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
                                        utf8_cp.push_back((char)(0x80 | (cp & 0x3F)));
                                    } else {
                                        utf8_cp.push_back((char)(0xF0 | (cp >> 18)));
                                        utf8_cp.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
                                        utf8_cp.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
                                        utf8_cp.push_back((char)(0x80 | (cp & 0x3F)));
                                    }
                                    KeyEvent ev;
                                    ev.kind = KeyEvent::Kind::Char;
                                    ev.ch = utf8_cp;
                                    return ev;
                                }
                            } catch (...) {}
                        }
                    }

                    return parse_escape_sequence(seq);
                }
                // Alt+key: treat as Escape.
                KeyEvent ev;
                ev.kind = KeyEvent::Kind::Escape;
                return ev;
            }
        }
        KeyEvent ev;
        ev.kind = KeyEvent::Kind::Escape;
        return ev;
    }

    // Not an escape sequence — assemble UTF-8 from the lead byte.
    auto utf8_seq_len = [](unsigned char b) -> int {
        if (b < 0x80)  return 1;   // ASCII
        if (b < 0xC0)  return -1;  // stray continuation byte (invalid)
        if (b < 0xE0)  return 2;
        if (b < 0xF0)  return 3;
        if (b < 0xF8)  return 4;
        return -1;
    };
    int seq_len = utf8_seq_len(static_cast<unsigned char>(first));
    std::string utf8_char;
    utf8_char.push_back(static_cast<char>(first));
    if (seq_len > 1) {
        for (int i = 1; i < seq_len; ++i) {
            unsigned char cb;
            ssize_t m = ::read(STDIN_FILENO, &cb, 1);
            if (m != 1 || (cb & 0xC0) != 0x80) {
                // V3.8: continuation byte missing — buffer it as pending
                // so the NEXT try_read_key call can prepend it. This
                // handles partial UTF-8 sequences at read boundaries
                // (mirrors Node.js StringDecoder).
                if (m == 1) {
                    utf8_pending_.push_back(static_cast<char>(cb));
                }
                break;
            }
            utf8_char.push_back(static_cast<char>(cb));
        }
    } else if (seq_len < 0) {
        // V3.8: stray continuation byte at the start — if we have a
        // pending buffer from before, the lead byte we needed was lost.
        // Replace the orphan with U+FFFD so downstream still gets a
        // printable grapheme instead of nothing.
        if (!utf8_pending_.empty()) {
            utf8_pending_.clear();
            utf8_char = "\xEF\xBF\xBD";  // U+FFFD
        } else {
            seq_len = 1;  // pass through as raw byte
        }
    }

    return classify(utf8_char);
}

void Terminal::on_resize(std::function<void(int, int)> cb) {
    on_resize_ = std::move(cb);
}

void Terminal::write(std::string_view bytes) {
    ssize_t total = 0;
    ssize_t len = static_cast<ssize_t>(bytes.size());
    const char* p = bytes.data();
    while (total < len) {
        ssize_t w = ::write(STDOUT_FILENO, p + total, len - total);
        if (w <= 0) break;
        total += w;
    }
}

void Terminal::flush() {
    // Line-buffered stdout is fine; no fd-level flush needed for raw stdout writes.
}

bool Terminal::is_tty() {
    return ::isatty(STDIN_FILENO) != 0 && ::isatty(STDOUT_FILENO) != 0;
}

}  // namespace pi::tui
