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
    // For Kind::Char this carries the **full UTF-8 character** (1-4
    // bytes), assembled by try_read_key() from the raw byte stream. For
    // ASCII input the string has length 1; for CJK / emoji it has length
    // 2-4. Use this, NOT a single char, to avoid splitting multibyte
    // sequences which corrupts text into replacement glyphs (e.g. typing
    // "你" produced "▒▒▒" because we treated each byte as its own
    // character).
    std::string ch;       // for Kind::Char (UTF-8, 1-4 bytes)
    std::string raw;   // for paste / unknown (raw bytes)

    bool is_char() const { return kind == Kind::Char; }
    bool is_escape() const { return kind == Kind::Escape; }
};

class Terminal {
public:
    Terminal();
    ~Terminal();

    // V3.7: Enable bracketed paste mode. When enabled, pasted text is
    // wrapped in ESC[200~ ... ESC[201~ and emitted as a single
    // KeyEvent{ kind=Paste, ch=<content> } instead of as N individual
    // Char events. Most modern terminals enable this by default, but
    // some need an explicit request.
    void enable_bracketed_paste_mode();
    void disable_bracketed_paste_mode();
    bool is_bracketed_paste_active() const { return bracketed_paste_active_; }

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
    bool bracketed_paste_active_ = false;
    // V3.8: StringDecoder-style buffer for partial UTF-8 sequences that
    // span read() boundaries. Contains the trailing 1-3 continuation
    // bytes that arrived without their lead byte. try_read_key() will
    // prepend this to the next byte read from stdin.
    mutable std::string utf8_pending_;
    std::function<void(int, int)> on_resize_;
};

}  // namespace pi::tui
