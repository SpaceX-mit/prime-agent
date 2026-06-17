# 2026-06-17 — V3 milestone (continue, JSONL, upstream-diff)

Status: ✅ Complete — committed `cc88a96`

## Goal
Ship the three V3 follow-ups called out in INC-003's "Recommended next
actions" section:

1. **V3.1** `run_agent_loop_continue()` proper implementation.
2. **V3.2** JSONL session persistence (multi-turn history survives restarts).
3. **V3.3** `tools/upstream-diff.sh` for periodic feature-gap detection.

## V3.1 — `run_agent_loop_continue`

### Before
```cpp
AgentLoopResult run_agent_loop_continue(const AgentLoopConfig& /*cfg*/) {
    return {{}, "not yet implemented"};
}
```

### After
```cpp
AgentLoopResult run_agent_loop_continue(const AgentLoopConfig& cfg) {
    if (cfg.messages.empty())
        return {{}, "cannot continue: empty history"};
    if (std::holds_alternative<AssistantMessage>(cfg.messages.back()))
        return {{}, "cannot continue: last message is AssistantMessage"};

    // Same loop logic as run_agent_loop, but starting from cfg.messages.
    AgentLoopResult res;
    res.messages = cfg.messages;
    // ... tool-call loop, error events emitted via message_end(stop_reason="error")
    return res;
}
```

### Tests added (`tests/test_streaming.cpp`)
- `continue_from_existing_user_message` — history `[User("hi")]` → assistant replies.
- `continue_rejects_assistant_last` — `[User("hi"), Assistant("hello")]` → error.
- `continue_rejects_empty` — `[]` → error.

### Configuration struct change
`AgentLoopConfig` gained a `messages` field; `run_agent_loop` was
unchanged but now uses the same loop body internally (refactored into a
shared helper to avoid duplication).

## V3.2 — JSONL session persistence

### New behavior in `interactive.cpp`
- On the first user turn of a session, `interactive.cpp` creates
  `~/.local/share/prime-agent/sessions/<id>.jsonl`.
- `SessionHeader` is written: `{ id, timestamp (UTC), cwd }`.
- On every `MessageEnd` event from the agent loop, the message is
  serialized via a `std::visit`-based `message_to_json` helper and
  appended to the JSONL via `SessionManager::append()` under a mutex.
- The file is opened once, held in the `ChatState`, closed when the
  session ends.

### ToolResultMessage serialization
`pi::ai::Message` is a `std::variant<UserMessage, AssistantMessage, ToolResultMessage>`.
Only the first two have `to_json()`. ToolResultMessage is serialized inline:
```cpp
j["role"] = "toolResult";
j["toolCallId"] = v.tool_call_id;
j["toolName"] = v.tool_name;
j["isError"] = v.is_error;
j["content"] = [...]  // text content array
```

### Out-of-scope (deferred to V4)
- Loading existing sessions at startup (currently `interactive.cpp` only
  *creates* new sessions; resuming a saved session is left for V4 once
  /resume UI is in place).
- Replaying a JSONL into a `run_agent_loop_continue` call.

## V3.3 — `tools/upstream-diff.sh`

A shell script that compares our repo against
`/tmp/pi-upstream` (the upstream TypeScript pi) and reports:

| Section | What it shows |
|---------|---------------|
| 1. Directory structure diff | `packages/*` (upstream) vs `libs/*` (us). |
| 2. Capability diff | Per-package keyword scan (streaming/tools/MCP/OAuth/compaction/extension/export HTML/session/TUI). |
| 3. API symbol diff | Upstream `export * from "./foo"` module list vs our `class`/`struct` declarations. |
| 4. Recommendations | Heuristic gaps (keyword upstream ⨯ absent locally) + known incident linkage. |

### Usage
```bash
# Default paths
./tools/upstream-diff.sh

# Custom upstream / our repo
./tools/upstream-diff.sh /path/to/pi /path/to/prime-agent
```

### Example output (excerpt)
```
==========================================
4. Recommendations
==========================================

Possible capability gaps (heuristic):
  - export HTML

Known feature gaps (from docs/incidents/):
| 2026-06-17 | INC-001 | Streaming events silently dropped ... | **Critical** | ✅ Fixed |
| 2026-06-17 | INC-002 | Interactive mode freezes ...            | **Critical** | ✅ Fixed |
| 2026-06-17 | INC-003 | Multi-turn conversation support missing | **Critical** | ✅ Fixed |
Plan/spec docs: 5 files in pi-spec/

Suggested next actions:
  - Verify HTML export with new model registry (already implemented)
  - Run 'pi -p <task>' in 3 modes: -p, --mode rpc, interactive
  - Run ctest; ensure 12+ suites pass
```

### Implementation gotchas
- Initially used `set -euo pipefail` which broke the script silently:
  the `grep ... | head -10` pipeline returned non-zero (zero matches in
  `tui/src/index.ts`) and `pipefail` aborted the script mid-section.
  Switched to `set -eu` (no `pipefail`) so empty grep results no longer
  exit.
- `awk -F'"' '{print $2}'` is used to extract the module path from
  `export * from "./foo";` because double-quoted `sed` regex was getting
  mangled by shell escaping.

## Verification

```bash
$ cd build && ctest --output-on-failure
…
100% tests passed, 0 tests failed out of 12
Total Test time (real) =   0.93 sec
```

Test-streaming subcounts (after V3 additions):
```
[continue] continues from user message: passed
[continue] rejects assistant as last: passed
[continue] rejects empty messages: passed
…
--- Summary ---
passed: 40
failed: 0
```

## Recommended next actions
- **V3.4**: implement `/resume <session-id>` to load a JSONL session into
  a fresh `ChatState` (uses `run_agent_loop_continue` to replay history).
- **V3.5**: replace `process_substitute` curl shim with libcurl for the
  upstream-diff script (currently uses `git clone` + local grep; should
  work with HTTPS for fetches too).
- **V3.6**: Markdown rendering (md4c vendored) so JSONL replays look
  reasonable in the TUI.
- **V3.7**: Windows port (`winsock2` socket adapter + libcurl swap-in).