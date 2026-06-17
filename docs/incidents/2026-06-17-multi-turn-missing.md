# INC-003: Multi-turn conversation support missing — each turn loses prior history

| Field | Value |
|---|---|
| **Date** | 2026-06-17 |
| **Reporter** | Duan Cheng (comparison with upstream pi) |
| **Severity** | **Critical** (core feature missing) |
| **Status** | ✅ Fixed |
| **Affected versions** | All V1 + V2 commits (until today) |

## TL;DR

In the C++ port, each `agent_loop` invocation treated its `initial_messages`
as a one-shot context. After the loop finished, the resulting assistant
message and tool results were thrown away — the next turn started with an
empty `state.history`. Users had to repeat the entire conversation context
every turn, or use `-c` to reload from disk (and even that wasn't wired into
interactive mode).

The upstream `pi-coding-agent` solves this by maintaining a persistent
`session.state.messages` array that grows with every turn. Our port did
not implement this mechanism.

Symptom (interactive mode):

```
> hello
Hi! How can I help?

> what did I just ask?
I don't have context for that — would you like to start a new conversation?
```

## Comparison with upstream

| Behavior | Upstream `pi` | Our port (pre-fix) |
|---|---|---|
| Persistent conversation state | ✅ `state.messages` | ❌ thrown away after each turn |
| Multi-turn within session | ✅ assistant sees all prior turns | ❌ only current turn |
| `message_end` appends to state | ✅ `state.messages.push(event.message)` | ❌ not implemented |
| `agentLoopContinue` for retries | ✅ | ❌ (skeleton only) |

### Upstream implementation (key snippet)

```typescript
// packages/agent/src/agent.ts
private processEvents(event: AgentEvent): void {
    switch (event.type) {
        case "message_end":
            this._state.streamingMessage = undefined;
            this._state.messages.push(event.message);   // <-- KEY
            break;
        ...
    }
}

// packages/agent/src/agent.ts
async prompt(input, images) {
    const messages = this.normalizePromptInput(input, images);
    await this.runPromptMessages(messages);
}
private runPromptMessages(messages) {
    await runAgentLoop(messages, this.createContextSnapshot(), ...);
    // context.messages === state.messages.slice() at start
}
```

The state.messages array grows with each `message_end` event.

## Root cause

Two compounding issues:

### 1. `AgentEvent::agent_end()` discarded its `messages` parameter

```cpp
// pre-fix
static AgentEvent agent_end(std::vector<pi::ai::Message> /*messages*/) {
    AgentEvent e; e.kind = Kind::AgentEnd; return e;
    // ^^ argument named but never stored
}
```

The event constructor took the full conversation history as a parameter
but the field was never added to the struct, so the data was lost.

### 2. `interactive.cpp` never copied agent results back to `state.history`

```cpp
// pre-fix interactive.cpp main loop
state.history.push_back(pi::ai::UserMessage{});   // user msg only
std::get<pi::ai::UserMessage>(state.history.back())
    .content.push_back(pi::ai::TextContent{text});
// ... agent runs ...
// NO: state.history.append(assistant_message)
// NO: state.history.append(tool_result_messages)
```

Even if the agent loop internally accumulated messages, the main loop only
kept the user message it had pushed ahead of time.

## Fix

### 1. `AgentEvent` carries the full history

```cpp
struct AgentEvent {
    // ... existing fields ...
    /// Full conversation history at this point (agent_end only). mutable
    /// so const event callbacks can read it after the agent thread populates it.
    mutable std::vector<pi::ai::Message> messages;

    static AgentEvent agent_end(std::vector<pi::ai::Message> messages) {
        AgentEvent e;
        e.kind = Kind::AgentEnd;
        e.messages = std::move(messages);   // <-- store
        return e;
    }
};
```

### 2. `interactive.cpp` persists on agent_end

```cpp
stream->drain([&](const pi::agent::AgentEvent& ev) {
    {
        std::lock_guard<std::mutex> g(state_mtx);
        switch (ev.kind) {
            // ... text/tool rendering ...
            case pi::agent::AgentEvent::Kind::AgentEnd:
                state.history = ev.messages;     // <-- persist
                break;
            // ...
        }
    }
});
```

Now `state.history` always reflects the full conversation including all
prior turns. Each subsequent prompt uses it as `initial_messages`.

### 3. `run_agent_loop_continue` (skeleton only for now)

```cpp
std::shared_ptr<AgentEventStream> run_agent_loop_continue(
    AgentLoopConfig config);
```

Header declared, body returns a `not yet implemented` error. The interactive
mode currently uses `run_agent_loop` with the full history (which is
sufficient). Continue support is documented as a follow-up action.

## Verification

### New regression test (`tests/test_streaming.cpp`)

```cpp
TEST_CASE("AgentEnd event carries full conversation history (multi-turn)") {
    // Drives agent_loop twice in sequence; verifies the second call sees
    // messages from the first call. This is the multi-turn invariant.

    // First turn: 1 user message -> expect 1 assistant response.
    auto stream1 = pi::agent::run_agent_loop(history1, cfg);
    std::vector<pi::ai::Message> captured;
    stream1->drain([&](const pi::agent::AgentEvent& e) {
        if (e.kind == pi::agent::AgentEvent::Kind::AgentEnd) {
            captured = e.messages;
        }
    });
    CHECK(captured.size() >= 2);   // user + assistant

    // Second turn: append new user message, run again.
    captured.push_back(userMessage2);
    auto stream2 = pi::agent::run_agent_loop(captured, cfg);
    std::vector<pi::ai::Message> final2;
    stream2->drain([&](const pi::agent::AgentEvent& e) {
        if (e.kind == pi::agent::AgentEvent::Kind::AgentEnd) {
            final2 = e.messages;
        }
    });

    CHECK(final2.size() >= 4);                                // grew
    CHECK(first.content.text == "first prompt");            // preserved
    CHECK(last_user.content.text == "second prompt");      // newest
}
```

### Test suite

```
$ cd build && ctest --output-on-failure
12/12 Test #12: test_streaming ...................   Passed    0.85 sec
100% tests passed, 0 tests failed out of 12
```

### Manual verification (in real interactive mode)

```
$ pi
> hello
Hi! How can I help?

> what did I just ask?
You said "hello" — a simple greeting.

> and my name?
You haven't told me your name yet — what should I call you?
```

## Impact assessment

This was a **core feature gap**, not a bug. Without multi-turn:

- Coding tasks require copying/pasting context every turn
- `/compact` (when it ran) had nothing to compact
- Compaction summary LLM calls wasted tokens on first-turn context
- Users discovered the limitation quickly and abandoned interactive mode

The fix is small (~20 lines across 3 files) but unlocks the central use case
of any LLM agent: sustained conversation.

## Lessons learned

### 1. Cross-check port completeness against upstream periodically

This gap existed since V1.0 (the first interactive-mode commit). No
upstream comparison was done — the port was tested feature-by-feature
but not workflow-by-workflow.

**Action**: add a periodic upstream-diff script to `tools/` that compares
each major capability (interactive mode, sessions, compaction, etc.)
and reports gaps.

### 2. State that "looks complete" isn't always useful

Both `run_agent_loop` (returns successfully) and `interactive.cpp`
(shows the answer) appeared to work in isolation. The bug was only
visible by trying a second turn — exactly the workflow users run.

**Action**: integration tests should mirror real user workflows, not
just unit-test individual functions.

### 3. Multi-turn should be the default API

Upstream's `state.messages` is the *primary* state; `prompt(text)` is
just a convenience that appends to it. In our port, `initial_messages`
felt like "the messages to use" — but the correct mental model is
"the existing conversation history plus this new turn".

**Action**: when porting stateful systems, design the API so the state
is owned by a single long-lived object, not threaded through function args.

## Follow-up actions

| Action | Owner | Status |
|---|---|---|
| Add multi-turn regression test | self | ✅ Done — `tests/test_streaming.cpp` |
| Implement `run_agent_loop_continue` properly | — | Pending (currently skeleton) |
| Add `tools/upstream-diff.sh` to detect feature gaps periodically | — | Pending |
| Persist multi-turn history to session JSONL on disk | — | Pending (V3) |
| `/new` slash command resets `state.history` | — | Already works (V2) |

## References

- Upstream: `packages/agent/src/agent.ts` (`processEvents`, `state.messages.push`)
- Upstream: `packages/agent/src/agent-loop.ts` (`runAgentLoop`, `agentLoopContinue`)
- Upstream: `packages/coding-agent/src/core/agent-session.ts` (`prompt`, `state.messages`)
- Fix commit: (pending — see git log)
- Related: INC-001 (streaming events race) — fixed in `67cd7d8`
- Related: INC-002 (interactive mode hangs) — fixed in `ab89f4c`
