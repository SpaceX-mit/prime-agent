// pi_core/unicode_width.hpp - Unicode East Asian Width
// Provides display width per Unicode 15.0 (simplified).
// CJK chars = 2, ASCII = 1, control = 0, surrogate = -1 (invalid).
#pragma once

#include <cstdint>
#include <string_view>

namespace pi::core::unicode {

/// Return the display width of a single Unicode code point.
/// Returns -1 if invalid (unpaired surrogate).
int display_width(char32_t cp) noexcept;

/// Return the total display width of a UTF-8 string.
int display_width(std::string_view utf8) noexcept;

/// True if the codepoint is a CJK / wide character.
bool is_wide(char32_t cp) noexcept;

}  // namespace pi::core::unicode
