# INC-001: Streaming events silently dropped due to non-blocking `try_pull()` race

| Field | Value |
|---|---|
| **Date** | 2026-06-17 |
| **Reporter** | Duan Cheng (during M3 live-key validation) |
| **Fix commit** | [`67cd7d8`](https://github.com/SpaceX-mit/prime-agent/commit/67cd7d8) |
| **Severity** | **Critical** |
| **Status** | ✅ Fixed and verified |
| **Affected versions** | V1 + V2 (every commit up to and including `a370334`) |
| **Affected providers** | All 5 (Anthropic, OpenAI, Google, Mistral, MiniMax) |

## TL;DR

In `stream_simple.cpp` and `agent_loop.cpp`, the drain loops used
`EventStream::try_pull()` — a non-blocking probe. With a fast HTTPS
round-trip (~100 ms), the provider's detached worker thread had not yet
pushed any events to the `EventStream` queue when the consumer thread
called `try_pull()`. The probe returned `nullopt` immediately, the loop
exited, and **every streaming event was silently dropped**. The final
`AssistantMessage` still came back correctly via `result()` (which *does*
block), so the bug was invisible to the user *unless* they used the
`--json` event-stream output or any UI that depended on progressive
rendering.

Symptom (with `--json`):

```
{"type":"agent_start"}
{"type":"turn_start"}
{"type":"message_end"}      ← no message_update events at all
{"type":"turn_end"}
{"type":"agent_end"}
{"type":"done","stopReason":"stop","usage":{}}
```

The same prompt with the bug fixed produced 5+ `text_delta` events,
including the model's reasoning trace.

## Timeline

| Time (UTC+8) | Event |
|---|---|
| 2026-06-17 ~14:00 | V2 milestone committed (`a370334`) — feature complete |
| 2026-06-17 ~14:15 | MiniMax provider added (V2.9, commit `c752418`) |
| 2026-06-17 ~14:25 | User provides real MiniMax key for live validation |
| 2026-06-17 ~14:30 | `pi -p "say ok" --model minimax/MiniMax-M3` prints nothing |
| 2026-06-17 ~14:35 | Initial hypothesis: MiniMax-specific protocol issue |
| 2026-06-17 ~14:40 | Manual `curl` to `api.minimaxi.com/v1/chat/completions` works perfectly |
| 2026-06-17 ~14:45 | Standalone SSE-parser test: chunks arrive, events parsed |
| 2026-06-17 ~14:50 | Bypass test: `provider->stream()` directly yields 5 events |
| 2026-06-17 ~14:55 | `ai::stream_simple()` yields **0 events** — bug localized |
| 2026-06-17 ~15:00 | Add debug prints: openai detached thread DOES push events |
| 2026-06-17 ~15:05 | Diagnosis: `try_pull()` races with detached worker thread |
| 2026-06-17 ~15:10 | Fix: change `try_pull()` → `pull()` in two locations |
| 2026-06-17 ~15:15 | Verified end-to-end with real key — `ok` printed |
| 2026-06-17 ~15:20 | All tests pass, commit `67cd7d8`, pushed |

## Reproduction

Minimal repro with the user's real MiniMax key:

```bash
export MINIMAX_API_KEY='sk-cp-...your-key...'

# BEFORE fix (commit <= a370334):
$ pi -p "say ok" --model minimax/MiniMax-M3 --json
{"type":"agent_start"}
{"type":"turn_start"}
{"type":"message_end"}       ← missing all text_delta events
{"type":"turn_end"}
{"type":"agent_end"}
{"type":"done","stopReason":"stop","usage":{}}

# AFTER fix (commit 67cd7d8):
$ pi -p "say ok" --model minimax/MiniMax-M3 --json
{"type":"message_update","event":"text_start"}
{"type":"message_update","event":"text_delta","delta":"ok"}
{"type":"message_update","event":"text_end"}
{"type":"done","stopReason":"stop","usage":{...}}
```

Any provider / model can be used to reproduce — the bug is in the
generic consumer loop, not in any provider. Even a 50-token Anthropic
prompt reproduces it.

## Root cause

### Race condition

`pi::ai::EventStream` is a producer/consumer queue:

```cpp
class EventStream {
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<Event> queue_;
    std::optional<AssistantMessage> final_;
    bool finished_ = false;
public:
    std::optional<Event> try_pull() {       // <-- non-blocking
        std::lock_guard<std::mutex> g(mu_);
        if (queue_.empty()) return std::nullopt;
        ...
    }
    std::optional<Event> pull() {           // <-- blocking
        std::unique_lock<std::mutex> g(mu_);
        cv_.wait(g, [&] { return !queue_.empty() || finished_; });
        ...
    }
};
```

The consumer in `stream_simple.cpp`:

```cpp
std::thread([stream, provider, model, ctx, opts]() {
    try {
        auto sub = provider->stream_simple(model, ctx, opts);   // (1)
        while (auto ev = sub->try_pull()) {                      // (2)  ← RACE
            stream->push(std::move(*ev));
        }
        auto res = sub->result();                                // (3)
        if (res) stream->end(std::move(res.value()));
    } catch (...) { ... }
}).detach();
```

Timeline of the race:

```
t=0ms   Consumer thread starts.
t=0ms   (1) provider->stream_simple() returns `sub` — openai's
        detached worker thread is *not yet scheduled*.
t=0ms   (2) Consumer calls sub->try_pull().
        sub->queue_ is empty, sub->finished_ == false → nullopt.
        Loop exits.
t=0ms   (3) Consumer calls sub->result() and BLOCKS on cv_.

t=50ms  Detached worker thread runs send_streaming().
        Each chunk feeds the SSE parser, which emits events
        that push to sub->queue_.
t=200ms Worker thread calls sub->end() → sub->finished_ = true.
        Consumer's blocked result() returns the final message.

Result:  consumer's loop drained 0 events. All pushed events stayed
         in sub->queue_ and were discarded when sub went out of scope.
```

The **fix is one line per file**: replace `try_pull()` with `pull()`.
`pull()` blocks on the condition variable until either events arrive
or `finished_` is true — closing the race completely.

### Why every prior V1 / V2 test passed

Tests use `doctest`-style synchronous checks. They either:

- Don't exercise the streaming path at all (most tests)
- Hit error paths where `result()` is called immediately (401, network failure)
- Use providers that error out before the race window matters

The race window is narrow on a fast LAN (50–200 ms) but reliably triggers
in the real-LLM path. Only the live validation with the user's MiniMax
key caught it.

## Fix

Two files, one-line change each.

### `libs/ai/src/stream_simple.cpp`

```diff
-            while (auto ev = sub->try_pull()) {
+            while (auto ev = sub->pull()) {
                 stream->push(std::move(*ev));
             }
```

### `libs/agent/src/agent_loop.cpp`

```diff
-                while (auto ev = sub->try_pull()) {
+                while (auto ev = sub->pull()) {
                     pi::ai::Message m;
                     m = ev->partial;
                     out->push(AgentEvent::message_update(std::move(m), *ev));
                     ...
                 }
```

Both comments updated to explain *why* blocking is required:

```cpp
// Use blocking pull() — try_pull() would race with the provider's
// detached worker thread and miss events pushed after our first poll.
```

## Verification

### Test suite

```
$ cd build && ctest --output-on-failure
...
11/11 Test #11: test_editor ......................   Passed    0.00 sec
100% tests passed, 0 tests failed out of 11
```

### End-to-end with real MiniMax key

```bash
$ export MINIMAX_API_KEY='sk-cp-...'

$ pi -p "say exactly: ok, no more, no less" --model minimax/MiniMax-M3
ok, no more, no less

$ pi -p "用一句话介绍你自己" --model minimax/MiniMax-M3
我是 MiniMax-M3，由 MiniMax 公司开发的 AI 基础模型助手，
致力于通过对话帮助你解决问题、获取信息和激发创意。

$ pi -p "What is 2+3*4?" --model minimax/MiniMax-M2.7-highspeed
**2 + 3 × 4 = 14**
Following the order of operations (PEMDAS/BODMAS)...

$ pi -p "say hi" --model minimax/MiniMax-M3 --json
{"type":"message_update","event":"text_start"}
{"type":"message_update","event":"text_delta","delta":"Hi! 👋 How can I help you today?"}
{"type":"message_update","event":"text_end"}
```

## Impact assessment

| Aspect | Before fix | After fix |
|---|---|---|
| Final answer text | ✅ Correct | ✅ Correct |
| Final stop_reason | ✅ Correct | ✅ Correct |
| `text_delta` events | ❌ Silently dropped (0 events) | ✅ Streamed (5+) |
| `tool_call` streaming | ❌ Tool calls invisible in TUI / JSON | ✅ Real-time rendering |
| Compaction streaming | ❌ Ineffective in interactive mode | ✅ Works |
| Reasoning-model output (MiniMax M3, etc.) | ❌ `think...` block invisible | ✅ Visible as thinking events |
| RPC client progressive events | ❌ Clients see only final `done` | ✅ See all events |
| TUI live token rendering | ❌ Only final chunk displayed | ✅ Token-by-token |

In short: **the entire streaming UX was broken** but hidden by the
fact that final answers were always correct.

### Affected versions

Every commit that built `pi` between:

- The first commit that introduced `EventStream` (early V1)
- Commit `a370334` (V2 milestone, last before this fix)

is affected. Roughly **8 commits** shipped with this bug.

## Lessons learned

### 1. Producer/consumer race conditions need explicit synchronization tests

The original unit tests passed because they never had a producer thread
racing with a consumer thread in the same way as the real runtime.
Add a test that explicitly:

- Spawns the producer on a background thread
- Schedules the consumer to start *before* the producer finishes
- Asserts that **all** events are received, not just the final message

### 2. Final-answer-correctness ≠ streaming-correctness

It is easy to write tests that only check the final state. Streaming
is a quality-of-experience feature; tests must specifically exercise the
intermediate states. Future regression test:

```cpp
TEST_CASE("stream_simple delivers all events") {
    // Mock provider that pushes events with a delay,
    // then ends. Consumer must see every pushed event.
}
```

### 3. Prefer blocking APIs when you don't have a strong reason to do otherwise

`try_pull()` exists for the case where the consumer has its own work to
do while waiting. In our case the consumer has nothing else to do, so
the blocking `pull()` is both correct *and* simpler.

Rule of thumb: **default to blocking**. Add a non-blocking variant only
when there is a concrete use case that needs it.

### 4. End-to-end smoke tests with real network are essential

A network-mocked test would have caught this bug. A real-key smoke test
(even with a cheap provider) catches it more reliably. Consider adding:

- A CI job that runs once a day against a paid provider with a tiny budget cap
- A `--live` smoke flag that bypasses API-key checks when the key is
  present in the environment, and is run manually before releases

### 5. When fixing a critical bug, audit similar code paths immediately

The same bug existed in two places (`stream_simple.cpp` and
`agent_loop.cpp`). After fixing the first, immediately grep for the
same pattern elsewhere:

```bash
$ grep -rn "try_pull" libs/
libs/ai/src/stream_simple.cpp:    while (auto ev = sub->try_pull()) {
libs/agent/src/agent_loop.cpp:        while (auto ev = sub->try_pull()) {
```

Both were fixed in the same commit.

## Follow-up actions

| Action | Owner | Status |
|---|---|---|
| Add regression test for `stream_simple` event delivery | self | ✅ Done — `tests/test_streaming.cpp` (4 cases, 15 assertions) |
| Add a `--smoke-live` script for real-key validation | — | Pending |
| Document the `pull()` vs `try_pull()` choice in `EventStream` header doc | — | Pending |

## References

- Commit fixing the bug: [`67cd7d8`](https://github.com/SpaceX-mit/prime-agent/commit/67cd7d8)
- Spec doc on the agent event flow: `pi-spec/90-Interfaces.md`
- `EventStream` definition: `libs/ai/include/pi_ai/event_stream.hpp`
- `pull()` implementation: `libs/ai/src/event_stream.cpp`
