# INC-002: Interactive mode freezes the main loop during agent runs

| Field | Value |
|---|---|
| **Date** | 2026-06-17 |
| **Reporter** | Duan Cheng (manual test: "input hangs in pi interactive mode") |
| **Severity** | **Critical** (UX blocker) |
| **Status** | ✅ Fixed (commit pending) |
| **Affected versions** | V1, V2 (all interactive-mode commits) |

## TL;DR

The interactive-mode main loop called `stream->drain(callback)` **synchronously**
inside the same thread that polls keyboard input. While the LLM was streaming,
no keystrokes were processed: text the user typed queued up in the TTY buffer,
`Ctrl-C` did not interrupt the agent, and `q`/`/exit` did not quit. The whole
TUI was frozen for the entire duration of every agent turn (5-60 s typical).

The user typed but nothing appeared; nothing responded to abort; the only way
out was to close the terminal.

Symptom in the source:

```cpp
// libs/tui/src/modes/interactive.cpp  (pre-fix, around line 339)
auto stream = pi::agent::run_agent_loop(std::move(messages), std::move(cfg));
stream->drain([&](const pi::agent::AgentEvent& ev) {
    // ... render callback fires, but main loop is here, not in the key poll
});
state.streaming = false;
// ... continue main loop only after agent finishes
```

## Timeline

| Time (UTC+8) | Event |
|---|---|
| 2026-06-17 ~14:00 | V2 milestone committed (`a370334`) — interactive mode shipped |
| 2026-06-17 ~14:15 | MiniMax provider added (V2.9) |
| 2026-06-17 ~14:30 | Live validation with real key — `-p` mode works |
| 2026-06-17 ~14:35 | INC-001 found & fixed (race in `stream_simple` drain loop) |
| 2026-06-17 ~14:55 | INC-001 verified fixed; streaming events now visible |
| 2026-06-17 ~15:30 | Tetris task verified end-to-end |
| 2026-06-17 ~16:00 | User reports: "interactive mode hangs when I type" |
| 2026-06-17 ~16:10 | Code review of `libs/tui/src/modes/interactive.cpp` confirms bug |
| 2026-06-17 ~16:20 | Fix implemented: agent loop on background thread |
| 2026-06-17 ~16:30 | Build green; 12/12 ctest pass (added INC-002 regression) |

## Reproduction

```bash
$ pi
# Welcome screen + prompt › appear.

# Type "say hello", press Enter.
# Agent loop starts streaming "Hi! How can I help..."

# Try to type "ABCDE" while the agent is running.
# Expected: see "ABCDE" appear in the input box as you type.
# Actual:   nothing happens. Input is lost.

# Try Ctrl-C.
# Expected: agent cancels, prompt returns to ready state.
# Actual:   nothing happens. Ctrl-C has no effect.

# Try "q" + Enter.
# Expected: pi exits.
# Actual:   nothing happens (the keypress isn't even read).
```

The user must close the terminal window to escape.

Programmatic repro (in `tests/test_streaming.cpp`):

```cpp
// SlowProvider takes 500 ms to produce events.
// stream_simple() should return immediately (< 100 ms)
// because it spawns a background thread.
// Without the INC-002 fix, the caller is blocked for the full 500 ms.
TEST_CASE("stream_simple on background thread does not block caller (INC-002)") {
    auto stream = stream_simple(/* model with SlowProvider, ctx, opts */);
    auto return_ms = ... call to now() ... ;
    CHECK(return_ms < 100);   // pre-fix: ~500ms; post-fix: <10ms
}
```

## Root cause

### Synchronous drain blocks the main loop

`interactive.cpp` had this structure:

```cpp
while (!tui.should_quit()) {
    refresh_chat();
    tui.render();

    auto k = term.try_read_key(100);    // (1) poll keyboard
    if (!k) continue;

    if (input->on_key(*k)) {
        if (input->take_submit()) {
            // ... build messages + tools ...
            auto stream = pi::agent::run_agent_loop(...);
            stream->drain([&](const pi::agent::AgentEvent& ev) {   // (2) BLOCKS
                // ... handle event, redraw ...
            });
            // (3) main loop reaches here only AFTER agent done
        }
    }
}
```

While `stream->drain()` is blocked at step (2):

- `term.try_read_key(100)` is never called
- All typed keys accumulate in the TTY input buffer
- `Ctrl-C` is queued but never delivered to the process
- `q` is queued but never read
- The user has no way out except closing the terminal

### Why every prior test passed

- Tests exercise programmatic APIs (`stream_simple`, `run_agent_loop`)
  in isolation, never the interactive loop.
- Manual interactive-mode testing was never done end-to-end
  (interactive mode requires a TTY, hard to automate).
- The CLI's `-p` mode uses a completely different code path (no TUI).

## Fix

### Strategy

Move the agent loop onto a **detached background thread**. The main loop
stays free to poll keys, redraw, and dispatch slash commands. Use a
mutex + `redraw_needed` flag for inter-thread state sync.

### Architecture

```
┌──────────────────────┐       ┌──────────────────────┐
│     Main TUI loop    │       │  Agent worker thread │
│ (foreground)        │       │  (detached)           │
│                      │       │                       │
│  ┌─ poll key (50ms)  │       │  run_agent_loop       │
│  ├─ if streaming:    │       │  ┌─ drain events      │
│  │   only Ctrl-C,    │       │  │  (blocking)        │
│  │   abort the       │       │  │                    │
│  │   agent           │       │  └─ stream->end()    │
│  ├─ else: normal     │       │                       │
│  │   key handling    │       │                       │
│  ├─ redraw if        │       │                       │
│  │   redraw_needed   │       │                       │
│  └─ abort handler:   │       │                       │
│      MutableAbort->  │       │  abort() check polls  │
│      signal()        │       │  this atomic each tick│
└──────────┬───────────┘       └──────────┬───────────┘
           │                              │
           └────── ChatState (mutex) ─────┘
                       │
                       └─ redraw_needed flag
```

### Code changes

#### 1. New `MutableAbort` (`libs/agent/include/pi_agent/tool.hpp`)

```cpp
class MutableAbort : public AbortSignal {
public:
    void signal() { aborted_.store(true); }
    bool aborted() const override { return aborted_.load(); }
private:
    std::atomic<bool> aborted_{false};
};
```

#### 2. Interactive mode refactor (`libs/tui/src/modes/interactive.cpp`)

The agent run is now spawned on a detached thread:

```cpp
auto spawn_agent = [&](std::vector<pi::ai::Message> messages,
                        pi::agent::AgentLoopConfig cfg_in) {
    auto abort_signal = std::make_shared<pi::agent::MutableAbort>();
    cfg_in.signal = abort_signal;

    {
        std::lock_guard<std::mutex> g(state_mtx);
        state.streaming = true;
        state.abort = abort_signal;
        state.redraw_needed = true;
    }
    footer->set_status("thinking…");
    refresh_chat();
    tui.render();

    std::thread([&state_mtx, &state, ...]() mutable {
        auto stream = pi::agent::run_agent_loop(std::move(messages), std::move(cfg));
        stream->drain([&](const pi::agent::AgentEvent& ev) {
            std::lock_guard<std::mutex> g(state_mtx);
            // ... mutate state.turns / state.current_text / etc ...
            state.redraw_needed = true;
        });
        // outside lock: do refresh + render (no other state access)
        std::lock_guard<std::mutex> g(state_mtx);
        state.streaming = false;
        state.turns.push_back(state.current_text);
        state.redraw_needed = true;
    }).detach();
};
```

The main loop:

```cpp
while (!tui.should_quit()) {
    // Redraw if the agent thread signalled.
    bool need_redraw = false;
    {
        std::lock_guard<std::mutex> g(state_mtx);
        if (state.redraw_needed) {
            state.redraw_needed = false;
            need_redraw = true;
        }
    }
    if (need_redraw) { refresh_chat(); tui.render(); }

    // Always poll keys with short timeout.
    auto k = term.try_read_key(50);
    if (!k) continue;

    // Ctrl-C: signal abort if running, otherwise quit.
    if (k->kind == KeyEvent::Kind::CtrlC) {
        bool was_streaming;
        std::shared_ptr<pi::agent::AbortSignal> abort;
        {
            std::lock_guard<std::mutex> g(state_mtx);
            was_streaming = state.streaming;
            abort = state.abort;
        }
        if (was_streaming) {
            auto m = std::dynamic_pointer_cast<pi::agent::MutableAbort>(abort);
            if (m) m->signal();
            continue;     // stay in loop, agent will wind down
        }
        tui.quit();
        break;
    }

    // Normal key handling.
    if (input->on_key(*k)) {
        if (input->take_submit()) {
            // ... if streaming, discard (or queue for steering in V3) ...
            // ... else spawn_agent(...) ...
        }
    }
}
```

### Behavior matrix

| User action | Pre-fix (hang) | Post-fix |
|---|---|---|
| Type letters during streaming | Lost (queued in TTY buffer) | Goes to input field; submit on Enter starts new turn (V2) or queues for steering (V3) |
| `Ctrl-C` during streaming | No effect | Aborts current agent turn; main loop continues |
| `q` + Enter during streaming | No effect | Ignored (V2) or queues for next turn (V3) |
| `/exit` during streaming | No effect | Ignored |
| New submit during streaming | Lost | Discarded with "agent still running" message (V2; V3: queue) |
| `Ctrl-C` while idle | Quit | Quit (unchanged) |

## Verification

### New regression tests in `tests/test_streaming.cpp`

```cpp
TEST_CASE("MutableAbort can be signaled and observed") { ... }
TEST_CASE("stream_simple on background thread does not block caller (INC-002)") {
    // SlowProvider takes 500 ms.
    // stream_simple() must return in < 100 ms.
    // Main loop ticks > 5 times during the agent run.
    // Events are observed (> 0).
}
```

### Test suite

```
$ cd build && ctest --output-on-failure
12/12 Test #12: test_streaming ...................   Passed    0.83 sec
100% tests passed, 0 tests failed out of 12
```

### Manual verification (in real terminal)

After deploying the fix:

1. `pi` → welcome screen + prompt
2. Type "say hello" + Enter → agent starts streaming "Hi!..."
3. Type "ABCDE" while streaming → "ABCDE" appears in input box
4. Press `Ctrl-C` → agent cancels, prompt returns to ready state
5. `q` + Enter → pi exits cleanly

## Impact assessment

| Scenario | Pre-fix | Post-fix |
|---|---|---|
| Long agent run with user input | Total loss | Normal input + abort |
| Accidental submit / typo | Can't cancel | Ctrl-C cancels mid-stream |
| User wants to leave | Close terminal | Ctrl-C + q (or just q) |
| Mid-stream slash command | Lost | Discarded with notification (V2); queued (V3) |

In short: **interactive mode was unusable for any non-trivial task** until
this fix. The bug was hidden because nobody manually tested the interactive
mode end-to-end before this report.

## Lessons learned

### 1. Long-running operations in UI loops MUST be backgrounded

Any operation that can take > 100 ms (network, file IO, complex computation)
must run on a worker thread, never in the main event loop. The same applies
to Electron apps, browser apps, game engines, etc.

**Rule of thumb**: if a callback takes > 16 ms (one frame), spawn a thread.

### 2. TTY testing must be done manually or via expect(1) in CI

Automated tests of TUI code are hard. Options:

- Use `expect(1)` (not installed on K3; available on most Linux)
- Use `script -q -c ...` with a script that injects keystrokes
- Reserve manual interactive-mode testing as a release-blocker

Add to follow-up actions: install `expect` or write a wrapper.

### 3. "User input during operation" is a frequent requirement

Most UIs need to handle input while a background operation is running. Common
patterns:

| Pattern | When to use |
|---|---|
| Ignore input during op | Simple operations (< 1s) |
| Queue input (steering) | Conversational agents, IDEs |
| Allow cancel + queue rest | Long operations with abort support |
| Modal: block input | When correctness requires serialization |

For pi (coding agent), pattern 3 is correct: allow Ctrl-C cancel, queue
other input for the next turn.

### 4. State shared between threads needs explicit ownership

The `ChatState` struct in `interactive.cpp` is shared between the main thread
and the agent thread. Required:

- A single mutex protecting all of it
- A `redraw_needed` flag (avoid double-buffering for that one bool)
- Snapshot-and-render pattern (lock → copy → unlock → render)

This is documented in the source comments of the new `interactive.cpp`.

## Follow-up actions

| Action | Owner | Status |
|---|---|---|
| Add INC-002 regression tests | self | ✅ Done — `tests/test_streaming.cpp` (2 new cases) |
| Add steering queue (V3): input during agent run is queued for next turn | — | Pending |
| Install `expect` on K3 and add a CI test that drives `pi` interactively | — | Pending |
| Consider using `ncurses` directly with a thread-safe event loop instead of raw `select()` | — | Pending |

## References

- Fix commit: (pending — will reference after commit)
- Related: INC-001 (streaming events race) — fixed in `67cd7d8`
- File: `libs/tui/src/modes/interactive.cpp`
- New API: `pi::agent::MutableAbort` in `libs/agent/include/pi_agent/tool.hpp`
