# 2026-06-17 — INC-006: Agent errors silently swallowed + V3.7-V3.10 input hardening

Status: ✅ Fixed (commit pending)

## INC-006: errors disappear

### Symptom
After submitting a prompt with the bad-UTF-8 input from INC-005 (or any
input that the LLM provider rejects), the input box clears but **nothing
else happens**. The system appears frozen. No error message, no chat
response, no retry. The user has no way to know what went wrong.

### Root cause
Two related gaps:

1. **`MessageEnd` handler only persisted to JSONL.** The agent loop
   emits `AgentEvent::MessageEnd` carrying the final assistant
   `Message`. In `libs/tui/src/modes/interactive.cpp` the handler did:
   ```cpp
   case AgentEvent::Kind::MessageEnd:
       append_message_entry_fn(ev.message);   // JSONL only
       break;
   ```
   The final assistant text was never appended to `state.current_text`,
   so `refresh_chat()` had nothing new to render. The user saw an empty
   area below their echoed prompt.

2. **Errors had no special handling.** When the provider sets
   `AssistantMessage.stop_reason = "error"` (HTTP 401, 400, timeout,
   parse failure), the message is delivered via `MessageEnd` like any
   other completion — same silent-drop path. Upstream pi handles this
   in `interactive-mode.ts` by checking `stopReason === "error"` and
   rendering `errorMessage` in red (`message_end` case, lines 2843+).

### Fix
In `interactive.cpp`'s `MessageEnd` case, extract the final text from
the assistant message and:
- Append it to `state.current_text` so it shows in chat.
- If `stop_reason == "error"` or `"aborted"`, append a red
  `[error: ...]` line containing `error_message`.
- Set `status_msg` so the footer reflects the failure mode.
- Mark `redraw = true` so the screen updates immediately.

```cpp
case AgentEvent::Kind::MessageEnd: {
    if (std::holds_alternative<AssistantMessage>(ev.message)) {
        const auto& am = std::get<AssistantMessage>(ev.message);
        std::string final_text;
        for (auto& c : am.content)
            if (std::holds_alternative<TextContent>(c))
                final_text += std::get<TextContent>(c).text;
        if (!final_text.empty()) state.current_text += final_text;
        if (am.stop_reason == "error" || am.stop_reason == "aborted") {
            state.current_text += "\n[" + am.stop_reason + ": "
                + (am.error_message ? *am.error_message : "(no detail)")
                + "]\n";
            status_msg = am.stop_reason;
        }
        redraw = true;
    }
    append_message_entry_fn(ev.message);
    break;
}
```

### PTY verification
With `OPENAI_API_KEY=sk-fake-will-fail-with-401` and the user prompt
` ▒▒你是谁`:
```
› ▒▒你是谁

[error: openai: HTTP 401: {
  "error": {
    "message": "Incorrect API key provided: sk-fake-**************-401...",
    "type": "invalid_request_error",
    "code": "invalid_api_key"
  },
  "status": 401
}]
```

Before the fix: nothing visible after `› ▒▒你是谁`.
After the fix: full error rendered in red.

---

## V3.7: Bracketed paste

### Problem
When the user pastes text from the clipboard, the terminal wraps it in
`ESC[200~...ESC[201~`. Without explicit handling, our `try_read_key`
parses `ESC[200~` as an unknown escape sequence (eats those 6 bytes)
and then reads each pasted byte one at a time as a separate Char event.
This works but:
- The user can no longer distinguish paste from fast typing.
- Some terminals (e.g. older xterm) send paste content unbracketed, in
  which case our existing byte-by-byte path works but emits N Char
  events for an N-character paste, triggering a render storm.

### Fix
1. On `enter_raw_mode()`, send `ESC[?2004h` to ask the terminal to use
   bracketed paste mode.
2. In `try_read_key()`, detect `ESC[200~` as a paste-start, accumulate
   bytes until `ESC[201~`, and emit a single `KeyEvent{ Kind::Paste,
   ch = <content> }`.
3. Add `KeyEvent::Kind::Paste` and handle it in `Input::on_key` /
   `Editor::on_key` to insert the whole content at once at the cursor.

### Tests
- `Input Paste event inserts full pasted text at once`
- `Input Paste of CJK text inserts all bytes correctly`
- `Editor Paste event inserts full pasted text`

---

## V3.8: StringDecoder-style buffering for partial UTF-8

### Problem
INC-005 fixed the case where the terminal sends bytes of a CJK
character together (one `read()` returns all 3 bytes of 你). But if the
kernel splits a CJK character across two `read()` calls — e.g. the
lead byte in one call and continuation bytes in the next — we'd see
stray continuation bytes (which are invalid as standalone characters)
and render them as `?` or drop them.

### Fix
Add `Terminal::utf8_pending_` (a `std::string`). When `try_read_key()`
exits with leftover continuation bytes, it parks them in `utf8_pending_`.
The next call prepends `utf8_pending_` to its incoming bytes before
classifying. If a stray continuation byte is encountered with no lead
byte, we replace it with `U+FFFD` (the standard Unicode replacement
character) rather than dropping it silently.

Mirrors Node.js's `StringDecoder` semantics (`prepend()` /
`text()`).

---

## V3.9: UTF-8 sanitization

### Problem
Even with INC-005 + V3.8, malformed UTF-8 could still enter the buffer
via:
- Paste from a webpage that misencoded the source.
- A buggy terminal sending invalid bytes.
- A custom input source bypassing our `try_read_key`.

Invalid UTF-8 in `messages[].content` is rejected by every major LLM
provider with a 400 Bad Request and (in our case) silently dropped —
back to INC-006 territory.

### Fix
A `sanitize_utf8()` helper in `input.cpp` and an inline copy in
`editor.cpp` that:
- Replaces orphan continuation bytes with `U+FFFD` (EF BF BD).
- Rejects overlong encodings (e.g. `C0 80` for U+0000).
- Rejects UTF-16 surrogate halves encoded as UTF-8 (`ED A0 80` etc.).
- Rejects codepoints > U+10FFFF.
- Truncates sequences that run off the end of the input.

Called from both `Input::Paste` and `Editor::Paste` handlers, plus
the interactive-mode `input->set_text(text)` path where we copy the
final user prompt into the assistant's input buffer.

### Tests
- `Input Paste sanitizes orphan continuation bytes`
- `Input Paste rejects overlong 2-byte encoding`
- `Input Paste truncates at end-of-string continuation bytes`

---

## V3.10: Kitty keyboard protocol

### Problem
Upstream pi gets its input correctness largely from the **Kitty
keyboard protocol** (`https://sw.kovidgoyal.net/kitty/keyboard-protocol/`).
With the `disambiguate escape codes` flag (1) enabled, terminals send
every key as a CSI-u sequence carrying the full Unicode codepoint,
bypassing the multi-byte UTF-8 assembly entirely. This means:
- CJK / emoji arrive as single codepoints, not bytes.
- Pressing `Ctrl-A` (mod=5) is unambiguously distinguishable from
  `a` (mod=1).
- IME composition candidates work correctly.

Without Kitty, we rely on UTF-8 byte assembly (INC-005/V3.8) which
works but is more fragile.

### Fix
1. On `enter_raw_mode()`, send `ESC[>1u` to enable Kitty
   `disambiguate escape codes`.
2. In `try_read_key()`, recognize CSI-u sequences of the form
   `ESC [ <codepoint> [:<shifted>] [;<mod>] [:<event>] u`. Parse the
   codepoint, reject Ctrl/Alt-modified keys (those go through normal
   keybinding paths), and emit a single `Char` event with the
   codepoint encoded as UTF-8. Skips the entire byte-assembly path.

### Test
- `Kitty CSI-u sequence: plain codepoint arrives as Char` — structural
  assertion on the parser regex / shape. Full integration tested via
  PTY (the terminal must support Kitty for this to be exercised).

---

## Combined verification

```
$ cd build && ctest --output-on-failure
…
100% tests passed, 0 tests failed out of 12
```

| Test file   | Assertions before | After | + new |
|-------------|------------------|-------|-------|
| test_editor |               25 |    51 |   +26 |
| test_streaming |             40 |    44 |    +4 |

PTY end-to-end with bad OpenAI key:
- Before: nothing visible after Enter.
- After: full `[error: openai: HTTP 401: ...]` rendered in red.

PTY end-to-end with valid UTF-8 input:
- Before: worked correctly (INC-005 fix).
- After: still works; now also handles paste + partial sequences +
  sanitization + Kitty-protocol fallback.

## Files touched

| File | Change |
|------|--------|
| `libs/tui/include/pi_tui/terminal.hpp` | `KeyEvent::Kind::Paste`, bracketed-paste API, `utf8_pending_` buffer |
| `libs/tui/src/terminal.cpp` | Bracketed paste detection, Kitty CSI-u, StringDecoder buffering, paste-mode enable |
| `libs/tui/src/components/input.cpp` | `sanitize_utf8()` helper, `Paste` handler, overlong/surrogate/too-big rejection |
| `libs/tui/src/components/editor.cpp` | `Paste` handler with inline sanitizer |
| `libs/tui/src/modes/interactive.cpp` | INC-006 fix: surface assistant text + error messages via `MessageEnd` handler |
| `tests/test_editor.cpp` | +7 test cases (Paste, sanitize, Kitty shape) |
| `tests/test_streaming.cpp` | +1 test case (error stream → `stop_reason="error"`) |