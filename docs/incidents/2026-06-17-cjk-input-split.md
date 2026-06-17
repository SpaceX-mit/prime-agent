# 2026-06-17 — INC-005: TUI input box splits UTF-8 multibyte characters

Status: ✅ Fixed (commit pending)

## Symptom

Typing CJK characters (Chinese / Japanese / Korean) or emoji in the
interactive-mode input box produces corrupted output. For example,
typing `你好` (hello) renders as `你▒  你` (with replacement glyphs and
double spacing). Worse, **the agent then never responds** to subsequent
inputs — the system appears frozen.

## Root cause

`Terminal::try_read_key()` read **one byte at a time** from stdin:

```cpp
unsigned char c;
ssize_t n = ::read(STDIN_FILENO, &c, 1);   // ← only 1 byte!
…
ev.kind = KeyEvent::Kind::Char;
ev.ch = c;                                  // ← single byte
```

When the user types `你` (U+4F60, encoded `E4 BD A0`), the terminal sends
**three separate bytes** in three separate `read()` calls. Each byte
became its own `KeyEvent::Char`, and `Input::on_key()` inserted each
byte as a separate character:

```cpp
text_.insert(…, ev.ch);  // ev.ch is 1 byte
cursor_++;               // cursor advances by 1 byte
```

The `text_` buffer therefore contained three orphan continuation bytes
(`\xE4\xBD\xA0`) interpreted as Latin-1, which the terminal rendered as
`▒` (U+2592 medium shade). Worse, the **left-arrow** and **backspace**
also operated one byte at a time, so the user could only "fix" the
corruption by deleting three bytes to remove one CJK character — and if
they typed more CJK in the meantime, the UTF-8 boundaries slipped and
text_ became invalid UTF-8 entirely.

When the user pressed Enter and submitted the corrupted buffer, the
provider's HTTP request contained an invalid UTF-8 string in the
`messages[0].content` field. The provider (depending on which one)
either:

- Rejected the request with a 400 Bad Request and we silently dropped it
  (no error message reached the UI), or
- Parsed enough to send back a malformed response, or
- Hung waiting for a tool-call JSON that could not be parsed.

Net effect: **the system appeared frozen**, exactly as the user reported.

This is also a comparison gap vs upstream pi: upstream sets
`process.stdin.setEncoding("utf8")` (terminal.ts:143) so Node.js's
`StringDecoder` assembles UTF-8 codepoints before they reach the
keypress parser. C++ has no equivalent; we must do it ourselves.

## Fix

### 1. `KeyEvent::Char` carries a full UTF-8 string (`terminal.hpp`)

```cpp
struct KeyEvent {
    enum class Kind { Char, Enter, … };
    Kind kind = Kind::Char;
    // For Kind::Char this carries the **full UTF-8 character** (1-4
    // bytes), assembled by try_read_key() from the raw byte stream.
    std::string ch;
    std::string raw;
};
```

### 2. `try_read_key()` assembles UTF-8 sequences (`terminal.cpp`)

After the first byte is read, peek the high bits to determine the
sequence length and read the continuation bytes (each must match
`10xxxxxx`). If a continuation byte is malformed, we stop early rather
than deadlock.

```cpp
auto utf8_seq_len = [](unsigned char b) -> int {
    if (b < 0x80)  return 1;
    if (b < 0xC0)  return -1;   // stray continuation
    if (b < 0xE0)  return 2;
    if (b < 0xF0)  return 3;
    if (b < 0xF8)  return 4;
    return -1;
};
for (int i = 1; i < seq_len; ++i) {
    unsigned char cb;
    if (::read(STDIN_FILENO, &cb, 1) != 1 || (cb & 0xC0) != 0x80) break;
    utf8_char.push_back(static_cast<char>(cb));
}
```

### 3. `Input::on_key()` inserts the full string (`input.cpp`)

```cpp
case KeyEvent::Kind::Char: {
    text_.insert(…cursor_…, ev.ch.begin(), ev.ch.end());
    cursor_ += ev.ch.size();   // ← advance by byte length
    …
}
```

### 4. `Left`/`Right`/`Backspace` move in grapheme units

`Backspace` now uses a small `utf8_prev()` helper to walk past
continuation bytes and erase the whole previous character in one go.
`Left`/`Right` walk past continuation bytes when advancing.

## Verification

### PTY end-to-end

```
=== after typing '你好' (ANSI-stripped) ===
…
› 你  
…
› 你好  
=== contains 你? === True
=== contains 好? === True
=== contains ▒ (U+2592)? === False
```

Each CJK character appears as one grapheme; no replacement glyphs;
agent receives valid UTF-8 input.

### Unit tests added (`tests/test_editor.cpp`)

| Test | Assertion |
|------|-----------|
| `Input insert UTF-8 multi-byte char as one grapheme` | Typing `你` (E4 BD A0) produces `text == "\xE4\xBD\xA0"`, `text.size() == 3` |
| `Input insert two CJK characters stays valid UTF-8` | Typing `你好` produces `text == "\xE4\xBD\xA0\xE5\xA5\xBD"`, `text.size() == 6` |
| `Input backspace removes whole CJK char, not one byte` | Typing `你a` + Backspace → `text == "\xE4\xBD\xA0"` (3 bytes), Backspace again → `text.empty()` |
| `Input Left arrow jumps over CJK char in one step` | Typing `你` + Left moves cursor from 3 → 0, not 3 → 2 |

### ctest

```
100% tests passed, 0 tests failed out of 12
```

`test_editor` went from 25 → 37 assertions (4 new test cases,
12 new assertions).

## Why we still don't match upstream perfectly

Upstream additionally:

1. Detects and prefers the **Kitty keyboard protocol** when the
   terminal supports it (`parseKittySequence`, `decodeKittyPrintable`).
   Under Kitty the terminal sends full Unicode codepoints as CSI-u
   escape sequences (`ESC [ <cp> u`), bypassing the UTF-8 byte-stream
   path entirely. Implementing this would give us correct behavior on
   terminals that misinterpret rapid multi-byte typing as multiple
   keypresses.

2. Uses a `StringDecoder` (via `setEncoding("utf8")`) that buffers
   partial UTF-8 sequences across `read()` boundaries. Our fix is
   simpler: we read all continuation bytes immediately, with no
   timeout-based peeking. This works for typical interactive typing
   (which is fast — bytes arrive together) but would fail if a CJK
   character were split across a kernel buffer boundary, which is
   rare in practice but possible.

## Recommended next actions

- **V3.7**: Detect and use the Kitty keyboard protocol
  (`CSI <cp> ; <mod> u`) when offered by the terminal. Would let us
  handle composition / IME candidates cleanly.
- **V3.8**: Use a `StringDecoder`-style buffered reader so partial
  UTF-8 sequences at the start of `try_read_key` are remembered
  until the rest arrives, rather than discarded.
- **V3.9**: Validate `text_` is well-formed UTF-8 after each
  insertion; if not, sanitize (replace orphans with U+FFFD). This
  protects the downstream provider from ever receiving invalid UTF-8
  in `messages[].content`.