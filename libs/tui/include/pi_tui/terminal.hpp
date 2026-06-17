// libs/tui/include/pi_tui/terminal.hpp
// Raw terminal mode + key event parsing.

#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace pi::tui {

/// A single key press.
struct KeyEvent {
    enum class Kind {
        Char,        // printable character
        Enter,
        Backspace,
        Tab,
        Escape,
        Up, Down, Left, Right,
        Home, End,
        PageUp, PageDown,
        Insert, Delete,
        F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
        CtrlA, CtrlB, CtrlC, CtrlD, CtrlE, CtrlF,
        CtrlG, CtrlH, CtrlI, CtrlJ, CtrlK, CtrlL, CtrlM, CtrlN,
        CtrlO, CtrlP, CtrlQ, CtrlR, CtrlS, CtrlT, CtrlU, CtrlV,
        CtrlW, CtrlX, CtrlY, CtrlZ,
        Resize,
        Paste,
        Unknown,
    };
    Kind kind = Kind::Char;
    char ch = 0;       // for Kind::Char
    std::string raw;   // for paste / unknown (raw bytes)

    bool is_char() const { return kind == Kind::Char; }
    bool is_escape() const { return kind == Kind::Escape; }
};

class Terminal {
public:
    Terminal();
    ~Terminal();

    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;

    /// Enter raw mode; install signal handlers. Idempotent.
    void enter_raw_mode();
    /// Restore the original terminal settings.
    void leave_raw_mode();
    bool is_raw() const { return raw_; }

    /// Query terminal size (rows, cols).
    std::pair<int, int> size() const;

    /// Read a single key event (blocking, no echo).
    KeyEvent read_key();

    /// Read a key event with a timeout in ms; returns nullopt on timeout.
    std::optional<KeyEvent> try_read_key(int timeout_ms);

    /// Set up SIGWINCH handling; call `cb` on resize.
    void on_resize(std::function<void(int, int)> cb);

    /// Write raw bytes to stdout (for rendering).
    void write(std::string_view bytes);
    void flush();

    /// Query whether stdin is a TTY.
    static bool is_tty();

private:
    bool raw_ = false;
    std::function<void(int, int)> on_resize_;
};

}  // namespace pi::tui
