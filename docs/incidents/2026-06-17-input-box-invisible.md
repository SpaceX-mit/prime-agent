# 2026-06-17 — INC-004: TUI input box doesn't show what the user types

Status: ✅ Fixed (commit pending)

## Symptom

When running `pi` in interactive mode, **the prompt box at the bottom of
the screen showed nothing** — typing characters had no visible effect.
Pressing Enter and submitting a (silent) message *did* trigger the agent,
but the user had no way to see or edit what they were sending.

Worse, even after submitting, the input box appeared to keep stale text
(or be empty), because `tui.render()` was not invoked when `input->set_text("")`
cleared it.

## Reproduction

```bash
# Start interactive mode (any provider — keys don't matter for this bug)
pi
# type "hello" → nothing visible in the prompt box
# press Enter  → agent runs, but the box was blank the whole time
```

PTY-driven reproduction confirms: before the fix, the line
`› h` / `› he` / `› hel` never appeared — only the empty prompt `›   ` was
ever written to the screen.

## Root cause

Two distinct gaps relative to upstream `pi`:

1. **No redraw on input change.** Upstream's `Editor` constructor
   receives the `TUI` reference (`new CustomEditor(this.ui, …)`), and
   every internal mutation (insert / delete / undo / paste) calls
   `this.tui.requestRender()`. The C++ port uses a separate `Input`
   component with no callback into the TUI, and the interactive-mode
   main loop only re-renders when the application-level
   `state.redraw_needed` flag is set — which is only ever true while
   the background agent thread is mutating `state.turns`. Between
   turns, **typing never triggered a render**.

2. **No cursor indicator.** Upstream's `Editor.render()` emits
   `\x1b[7m<grapheme>\x1b[0m` (reverse-video) over the character under
   the cursor (or a trailing space if EOL). The C++ port's
   `Input::render()` just appended `text_` without any cursor marker.

## Fix

### 1. `interactive.cpp` — call `tui.render()` after every keypress

```cpp
if (input->on_key(*k)) {
    // INC-004: must re-render the screen or the user won't see what
    // they typed (mirror upstream Editor's tui.requestRender()).
    tui.render();
    if (input->take_submit()) { … }
}
```

`TUI::render()` already short-circuits via `prev_frame_` equality, so
the extra render is bounded to one string comparison per keystroke.

### 2. `input.cpp` — inverse-video cursor glyph

```cpp
std::string before(text_, 0, cursor_);
std::string after (text_, cursor_, std::string::npos);
std::string cursor_glyph = after.empty()
    ? std::string(" ")
    : first_grapheme(after);   // UTF-8 safe
std::string after_rest = after.substr(cursor_glyph.size());
line += before;
line += "\x1b[7m"; line += cursor_glyph; line += "\x1b[0m";
line += after_rest;
```

The first-grapheme extraction walks past UTF-8 continuation bytes
(`0b10xxxxxx`) so multi-byte characters don't get split.

## Verification

### PTY test (real binary)

```
=== after typing 'hello' (raw ANSI-stripped) ===
…
› h  
…
› he  
…
› hel  
…
```

Each keystroke produces a new screen with one more visible character.

### Unit tests added (`tests/test_editor.cpp`)

| Test | Assertion |
|------|-----------|
| `Input render includes typed text` | `lines[0].find("hi") != npos` after typing "hi" |
| `Input render shows cursor (inverse-video) at cursor position` | `lines[0].find("\x1b[7m") != npos` |
| `Input render with cursor in middle highlights that grapheme` | `lines[0].find("hel\x1b[7ml\x1b[0mo") != npos` |
| `Input backspace updates render output` | After backspace, "x" remains, "xy" gone |

### ctest

```
12/12 Test #12: test_streaming ...................   Passed    0.86 sec
…
100% tests passed, 0 tests failed out of 12
```

`test_editor` went from 16 → 25 assertions (4 new test cases,
9 new assertions).

## Why we still use `Input` instead of `Editor`

We have a full-featured `Editor` component (`libs/tui/src/components/editor.cpp`,
237 lines) with multi-line support, undo, autocomplete, and paste
handling — mirroring the upstream TS implementation closely. But:

- `interactive.cpp` uses `Input` (single-line, 96 LOC) for V1/V2 because
  it was sufficient and tested first.
- `Editor` is exercised by `test_editor` (25 assertions) but not yet
  wired into the live UI.

A future V4 change can swap `Input` → `Editor` and gain multi-line
editing (useful for `/compact` settings edits, `/login` provider
prompts, etc.) — the `Editor::on_key` API is already drop-in compatible.

## Recommended next actions

- **V3.4**: Swap `Input` → `Editor` in `interactive.cpp`; remove
  `Input` component entirely. This gives multi-line editing for free.
- **V3.5**: Re-render only when the **frame actually changed** (today
  we render on every keystroke; cheap but wasteful at high typing
  speeds). Use a more granular dirty flag on the `Input` component
  itself rather than re-rendering the whole tree.
- **V3.6**: Pull in `tui.requestRender()` semantics — debounce to e.g.
  16ms (60fps) to coalesce multiple keystrokes between paints.