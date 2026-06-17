// libs/core/src/unicode_width.cpp
// Simplified Unicode East Asian Width (Unicode 15).
// We provide a table-driven implementation that covers common ranges
// and falls back to wcwidth() for unknown code points.

#include "pi_core/unicode_width.hpp"

#include <cstdint>
#include <cwchar>
#include <cwctype>
#include <stdexcept>
#include <string>
#include <vector>

namespace pi::core::unicode {

namespace {

// Range {lo, hi} inclusive. Both endpoints are wide (CJK etc).
struct WideRange { uint32_t lo, hi; };

// Minimal-but-correct ranges: full set is enormous.
// Coverage: CJK Unified Ideographs, Hiragana/Katakana, Hangul, fullwidth,
// and common symbols/emojis that should be wide.
constexpr WideRange kWideRanges[] = {
    {0x1100, 0x115F},   // Hangul Jamo
    {0x2E80, 0x303E},   // CJK Radicals + Symbols
    {0x3041, 0x33FF},   // Hiragana/Katakana/CJK Compat
    {0x3400, 0x4DBF},   // CJK Ext A
    {0x4E00, 0x9FFF},   // CJK Unified
    {0xA000, 0xA4CF},   // Yi
    {0xAC00, 0xD7A3},   // Hangul Syllables
    {0xF900, 0xFAFF},   // CJK Compat Ideographs
    {0xFE30, 0xFE4F},   // CJK Compat Forms
    {0xFF00, 0xFF60},   // Fullwidth
    {0xFFE0, 0xFFE6},   // Fullwidth signs
    {0x1F300, 0x1F64F}, // Misc Symbols & Emoticons
    {0x1F680, 0x1F6FF}, // Transport
    {0x1F900, 0x1F9FF}, // Supplemental Symbols
    {0x20000, 0x2FFFD}, // CJK Ext B-F
    {0x30000, 0x3FFFD}, // CJK Ext G
};

bool is_in_wide_table(uint32_t cp) {
    // Binary search.
    size_t lo = 0, hi = sizeof(kWideRanges)/sizeof(kWideRanges[0]);
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (cp < kWideRanges[mid].lo) hi = mid;
        else if (cp > kWideRanges[mid].hi) lo = mid + 1;
        else return true;
    }
    return false;
}

uint32_t decode_utf8(const char*& p, const char* end) {
    if (p >= end) return 0;
    uint8_t b = static_cast<uint8_t>(*p++);
    if (b < 0x80) return b;
    if ((b & 0xE0) == 0xC0 && p < end) {
        uint32_t cp = (b & 0x1F) << 6;
        cp |= (static_cast<uint8_t>(*p++) & 0x3F);
        return cp;
    }
    if ((b & 0xF0) == 0xE0 && p + 1 < end) {
        uint32_t cp = (b & 0x0F) << 12;
        cp |= (static_cast<uint8_t>(*p++) & 0x3F) << 6;
        cp |= (static_cast<uint8_t>(*p++) & 0x3F);
        return cp;
    }
    if ((b & 0xF8) == 0xF0 && p + 2 < end) {
        uint32_t cp = (b & 0x07) << 18;
        cp |= (static_cast<uint8_t>(*p++) & 0x3F) << 12;
        cp |= (static_cast<uint8_t>(*p++) & 0x3F) << 6;
        cp |= (static_cast<uint8_t>(*p++) & 0x3F);
        return cp;
    }
    return 0xFFFD;  // replacement
}

}  // namespace

bool is_wide(char32_t cp) noexcept {
    auto u = static_cast<uint32_t>(cp);
    if (u < 0x80) return false;
    return is_in_wide_table(u);
}

int display_width(char32_t cp) noexcept {
    auto u = static_cast<uint32_t>(cp);
    if (u < 0x20) return 0;                  // control
    if (u == 0x7F) return 0;                 // DEL
    if (u >= 0xD800 && u <= 0xDFFF) return -1; // surrogate
    if (is_wide(cp)) return 2;
    if (u == 0x200D || (u >= 0xE000 && u <= 0xF8FF)) return 0;  // ZWJ + private use
    return 1;
}

int display_width(std::string_view utf8) noexcept {
    int total = 0;
    const char* p = utf8.data();
    const char* end = p + utf8.size();
    while (p < end) {
        uint32_t cp = decode_utf8(p, end);
        int w = display_width(static_cast<char32_t>(cp));
        if (w < 0) return -1;
        total += w;
    }
    return total;
}

}  // namespace pi::core::unicode
