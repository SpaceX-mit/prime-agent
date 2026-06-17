// pi_core/ansi.hpp - ANSI escape codes
#pragma once

#include <string>
#include <string_view>

namespace pi::core::ansi {

// CSI sequences
constexpr const char* ESC = "\x1b";
constexpr const char* CSI = "\x1b[";
constexpr const char* RESET = "\x1b[0m";
constexpr const char* BOLD = "\x1b[1m";
constexpr const char* DIM = "\x1b[2m";
constexpr const char* ITALIC = "\x1b[3m";
constexpr const char* UNDERLINE = "\x1b[4m";
constexpr const char* REVERSE = "\x1b[7m";

inline std::string sgr(int n) {
    std::string r = "\x1b[";
    r += std::to_string(n);
    r += 'm';
    return r;
}

inline std::string fg(int n) { return sgr(30 + n); }   // 30..37
inline std::string bg(int n) { return sgr(40 + n); }   // 40..47
inline std::string fg_bright(int n) { return sgr(90 + n); }  // 90..97
inline std::string bg_bright(int n) { return sgr(100 + n); } // 100..107

inline std::string rgb_fg(int r, int g, int b) {
    return "\x1b[38;2;" + std::to_string(r) + ";"
           + std::to_string(g) + ";" + std::to_string(b) + "m";
}
inline std::string rgb_bg(int r, int g, int b) {
    return "\x1b[48;2;" + std::to_string(r) + ";"
           + std::to_string(g) + ";" + std::to_string(b) + "m";
}

inline std::string move_cursor(int row, int col) {
    return "\x1b[" + std::to_string(row) + ";" + std::to_string(col) + "H";
}

inline std::string clear_screen() { return "\x1b[2J\x1b[H"; }
inline std::string clear_line() { return "\x1b[2K"; }
inline std::string hide_cursor() { return "\x1b[?25l"; }
inline std::string show_cursor() { return "\x1b[?25h"; }

inline std::string enter_alt_screen() { return "\x1b[?1049h"; }
inline std::string exit_alt_screen() { return "\x1b[?1049l"; }

inline std::string enable_mouse_sgr() { return "\x1b[?1006h\x1b[?1003h"; }
inline std::string disable_mouse_sgr() { return "\x1b[?1003l\x1b[?1006l"; }

/// OSC 9;4 progress: <state>;<progress 0-100>
inline std::string osc9_progress(int state, int progress) {
    return "\x1b]9;4;" + std::to_string(state) + ";"
           + std::to_string(progress) + "\x1b\\";
}
inline std::string osc9_clear() { return "\x1b]9;4;0;0\x1b\\"; }

}  // namespace pi::core::ansi
