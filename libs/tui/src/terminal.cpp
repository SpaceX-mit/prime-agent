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

KeyEvent classify(char c) {
    KeyEvent ev;
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
    ev.kind = KeyEvent::Kind::Char;
    ev.ch = c;
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
    flush();
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

    unsigned char c;
    ssize_t n = ::read(STDIN_FILENO, &c, 1);
    if (n <= 0) return std::nullopt;

    if (c == 0x1B) {
        // Could be escape sequence or just escape.
        // Peek for more bytes.
        fd_set rf;
        FD_ZERO(&rf); FD_SET(STDIN_FILENO, &rf);
        struct timeval t{0, 30'000};  // 30ms
        if (::select(STDIN_FILENO + 1, &rf, nullptr, nullptr, &t) > 0) {
            unsigned char c2;
            if (::read(STDIN_FILENO, &c2, 1) == 1) {
                if (c2 == '[' || c2 == 'O') {
                    std::string seq; seq.push_back(static_cast<char>(c2));
                    // Read the rest of the sequence.
                    while (true) {
                        fd_set rf3; FD_ZERO(&rf3); FD_SET(STDIN_FILENO, &rf3);
                        struct timeval t3{0, 30'000};
                        if (::select(STDIN_FILENO + 1, &rf3, nullptr, nullptr, &t3) <= 0) break;
                        unsigned char c3;
                        if (::read(STDIN_FILENO, &c3, 1) != 1) break;
                        seq.push_back(static_cast<char>(c3));
                        if (c3 >= 0x40 && c3 <= 0x7E) break;  // final byte
                    }
                    return parse_escape_sequence(seq);
                }
                // It's just an Alt+key; treat as Escape + char
                KeyEvent ev;
                ev.kind = KeyEvent::Kind::Escape;
                return ev;
            }
        }
        KeyEvent ev;
        ev.kind = KeyEvent::Kind::Escape;
        return ev;
    }

    return classify(static_cast<char>(c));
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
