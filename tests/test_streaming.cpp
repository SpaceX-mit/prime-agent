// tests/test_streaming.cpp
//
// Regression test for INC-001: streaming events must not be silently
// dropped due to non-blocking try_pull() race.
//
// We verify the core invariant directly at the EventStream level:
// a consumer thread that uses blocking pull() must observe every event
// pushed by a producer thread on a slow provider.
//
// Pre-fix, stream_simple used try_pull() in a loop, which could return
// nullopt before the producer had a chance to push any events. After
// the fix (pull() instead of try_pull()), the consumer blocks on the
// condition variable until events arrive.

#define TEST_FRAMEWORK_IMPLEMENT
#include "test_framework.hpp"

#include "pi_ai/event_stream.hpp"
#include "pi_ai/models.hpp"
#include "pi_ai/provider.hpp"
#include "pi_ai/stream_simple.hpp"
#include "pi_ai/types.hpp"
#include "pi_agent/agent_loop.hpp"
#include "pi_agent/tool.hpp"

using namespace pi::ai;

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>


namespace {

/// Spawn a producer thread that pushes N text-delta events after a delay,
/// then ends the stream. Returns the EventStream + a flag to wait for the
/// producer to finish.
struct ProducerHandle {
    std::shared_ptr<EventStream> stream;
    std::thread thread;
};

ProducerHandle spawn_producer(std::vector<std::string> chunks, int delay_ms) {
    auto out = std::make_shared<EventStream>();
    ProducerHandle h{out, std::thread([chunks = std::move(chunks), delay_ms, out]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));

        AssistantMessage partial;
        out->push(AssistantMessageEvent::start(partial));

        int idx = 0;
        TextContent tc;
        tc.text = chunks[0];
        partial.content.push_back(tc);
        out->push(AssistantMessageEvent::text_start(idx, partial));
        for (size_t i = 1; i < chunks.size(); ++i) {
            std::get<TextContent>(partial.content.back()).text += chunks[i];
            out->push(AssistantMessageEvent::text_delta(idx, chunks[i], partial));
        }
        std::string full_text = std::get<TextContent>(partial.content.back()).text;
        out->push(AssistantMessageEvent::text_end(idx, full_text, partial));
        partial.stop_reason = "stop";
        out->push(AssistantMessageEvent::done("stop", partial));
        out->end(std::move(partial));
    })};
    return h;
}

/// Consumer that uses the blocking pull() (post-fix behavior).
struct ConsumerStats {
    int deltas = 0;
    int starts = 0;
    int ends = 0;
    int dones = 0;
    std::string assembled;
    std::string stop_reason;
};

ConsumerStats drain_with_pull(std::shared_ptr<EventStream> stream) {
    ConsumerStats s;
    while (auto ev = stream->pull()) {
        switch (ev->kind) {
            case AssistantMessageEvent::Kind::TextStart:  s.starts++;  break;
            case AssistantMessageEvent::Kind::TextDelta:  s.deltas++;  s.assembled += ev->delta; break;
            case AssistantMessageEvent::Kind::TextEnd:    s.ends++;    break;
            case AssistantMessageEvent::Kind::Done:       s.dones++;   s.stop_reason = ev->reason; break;
            default: break;
        }
    }
    return s;
}

/// Consumer that uses the non-blocking try_pull() in a tight loop
/// (pre-fix behavior). Then calls result() which blocks until end.
struct TryPullStats {
    int deltas = 0;
    int tries = 0;
    std::string assembled;
};

TryPullStats drain_with_try_pull(std::shared_ptr<EventStream> stream) {
    TryPullStats s;
    while (true) {
        s.tries++;
        auto ev = stream->try_pull();
        if (!ev) break;
        if (ev->kind == AssistantMessageEvent::Kind::TextDelta) {
            s.deltas++;
            s.assembled += ev->delta;
        }
    }
    return s;
}

}  // namespace

TEST_CASE("pull() delivers all events even with slow producer (INC-001 regression)") {
    auto chunks = std::vector<std::string>{"Hello", ", ", "world", "!"};

    // Run the consumer in a separate thread so we can race against the
    // producer's startup latency.
    auto h = spawn_producer(chunks, /*delay_ms=*/50);
    auto stats = drain_with_pull(h.stream);
    h.thread.join();

    CHECK(stats.deltas == (int)chunks.size() - 1);  // first chunk goes to text_start
    CHECK(stats.starts == 1);
    CHECK(stats.ends == 1);
    CHECK(stats.dones == 1);
    CHECK(stats.stop_reason == "stop");
    CHECK(stats.assembled == ", world!");
}

TEST_CASE("try_pull() (pre-fix behavior) loses events with slow producer") {
    auto chunks = std::vector<std::string>{"Hello", ", ", "world", "!"};

    auto h = spawn_producer(chunks, /*delay_ms=*/50);
    auto stats = drain_with_try_pull(h.stream);
    h.thread.join();

    // Pre-fix behavior: try_pull() returns nullopt immediately because
    // the producer hasn't pushed yet. Loop exits with 0 events.
    CHECK(stats.tries == 1);          // exactly one probe, then exit
    CHECK(stats.deltas == 0);          // ALL events lost
    CHECK(stats.assembled == "");
}

TEST_CASE("pull() with very slow producer (race window maximized)") {
    // 200 ms startup latency — well past any try_pull() probe.
    auto chunks = std::vector<std::string>{"a","b","c","d","e","f","g","h","i","j"};
    auto h = spawn_producer(chunks, /*delay_ms=*/200);
    auto stats = drain_with_pull(h.stream);
    h.thread.join();

    CHECK(stats.deltas == 9);  // 10 chunks, first goes to text_start
    CHECK(stats.assembled == "bcdefghij");
    CHECK(stats.stop_reason == "stop");
}

TEST_CASE("EventStream result() returns final message after stream end") {
    auto chunks = std::vector<std::string>{"done"};
    auto h = spawn_producer(chunks, /*delay_ms=*/10);

    auto r = h.stream->result();
    CHECK(r.is_ok());
    h.thread.join();

    std::string text;
    for (auto& c : r.value().content) {
        if (std::holds_alternative<TextContent>(c)) {
            text += std::get<TextContent>(c).text;
        }
    }
    CHECK(text == "done");
    CHECK(r.value().stop_reason == "stop");
}

// ===========================================================================
// INC-002: interactive mode must not block the main loop on agent run
// ===========================================================================

TEST_CASE("MutableAbort can be signaled and observed") {
    auto a = std::make_shared<pi::agent::MutableAbort>();
    CHECK_FALSE(a->aborted());
    a->signal();
    CHECK(a->aborted());
}

// Custom scripted provider that takes a controllable amount of time
// so we can verify the main thread is NOT blocked while the agent runs.
namespace {
class SlowProvider : public Provider {
public:
    SlowProvider(int delay_ms) : delay_ms_(delay_ms) {}
    ApiKind api() const override { return ApiKind::OpenAICompletions; }
    std::string name() const override { return "slow-test"; }
    std::shared_ptr<EventStream> stream(
        const Model&, const Context&, const StreamOptions&) override {
        auto out = std::make_shared<EventStream>();
        std::thread([out, delay_ms = delay_ms_]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            (void)delay_ms;
            AssistantMessage m;
            out->push(AssistantMessageEvent::start(m));
            out->push(AssistantMessageEvent::text_start(0, m));
            TextContent tc; tc.text = "done";
            m.content.push_back(tc);
            out->push(AssistantMessageEvent::text_delta(0, "done", m));
            out->push(AssistantMessageEvent::text_end(0, "done", m));
            m.stop_reason = "stop";
            out->push(AssistantMessageEvent::done("stop", m));
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            out->end(std::move(m));
        }).detach();
        return out;
    }
private:
    int delay_ms_;
};
}  // namespace

TEST_CASE("stream_simple on background thread does not block caller (INC-002)") {
    // Build the SlowProvider directly and call its stream() through a
    // shim that mimics what stream_simple does — a background thread
    // that drains the provider's EventStream into a returned one.
    // This proves the caller is not blocked while the agent runs.

    auto slow = std::make_shared<SlowProvider>(/*delay_ms=*/500);

    // Mimic stream_simple: spawn a background thread to drain sub.
    // We use OpenAICompletionsProvider-style forwarding to keep this
    // test independent of any registered provider.
    // Build a minimal "agent" by using SlowProvider directly.

    // Register SlowProvider under a unique name for this test only.
    static bool registered = false;
    if (!registered) {
        ProviderRegistry::instance().register_provider(slow);
        registered = true;
    }

    // Find a model that matches SlowProvider's name.
    // (We don't have such a model in builtin_models, so we synthesize one.)
    Model m;
    m.id = "slow-test-model";
    m.provider = slow->name();
    m.api = ApiKind::OpenAICompletions;

    Context ctx;
    UserMessage um;
    um.content.push_back(TextContent{"hi"});
    ctx.messages.push_back(um);
    SimpleStreamOptions sopts;
    sopts.api_key = "fake";

    auto t0 = std::chrono::steady_clock::now();
    auto stream = stream_simple(m, ctx, sopts);
    auto t1 = std::chrono::steady_clock::now();
    auto return_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    // stream_simple should return in <100ms even though the provider
    // takes 500ms internally — because the work is on a background thread.
    CHECK(return_ms < 100);

    // Simulate the fixed interactive-mode main loop: drain events
    // non-blockingly while the agent runs.
    int main_loop_ticks = 0;
    int total_events = 0;
    while (!stream->finished()) {
        // Drain all available events.
        while (auto ev = stream->try_pull()) ++total_events;
        ++main_loop_ticks;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (std::chrono::steady_clock::now() - t0 > std::chrono::seconds(3)) {
            break;  // safety bail
        }
    }
    // Drain any remaining events.
    while (auto ev = stream->try_pull()) ++total_events;

    // The agent took ~510ms total. With 10ms ticks, main loop should
    // have ticked ~50 times — definitely more than 5.
    CHECK(main_loop_ticks > 5);
    CHECK(total_events > 0);
}

// ===========================================================================
// Multi-turn conversation: agent_end carries full history, next turn
// sees prior turns.
// ===========================================================================

TEST_CASE("AgentEnd event carries full conversation history (multi-turn)") {
    // Drives agent_loop twice in sequence; verifies the second call sees
    // messages from the first call. This is the multi-turn invariant that
    // upstream pi maintains via state.messages.push(event.message).

    // Local scripted provider (the file-level one is in an anonymous
    // namespace and not visible here).
    class LocalProvider : public Provider {
    public:
        LocalProvider(std::vector<std::string> chunks, int delay_ms)
            : chunks_(std::move(chunks)), delay_ms_(delay_ms) {}
        ApiKind api() const override { return ApiKind::OpenAICompletions; }
        std::string name() const override { return "scripted-multi"; }
        std::shared_ptr<EventStream> stream(
            const Model&, const Context&, const StreamOptions&) override {
            auto out = std::make_shared<EventStream>();
            std::thread([out, chunks = chunks_, delay_ms = delay_ms_]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
                AssistantMessage m;
                out->push(AssistantMessageEvent::start(m));
                out->push(AssistantMessageEvent::text_start(0, m));
                TextContent tc;
                tc.text = chunks[0];
                m.content.push_back(tc);
                out->push(AssistantMessageEvent::text_delta(0, chunks[0], m));
                out->push(AssistantMessageEvent::text_end(0, chunks[0], m));
                m.stop_reason = "stop";
                out->push(AssistantMessageEvent::done("stop", m));
                out->end(std::move(m));
            }).detach();
            return out;
        }
    private:
        std::vector<std::string> chunks_;
        int delay_ms_;
    };

    static bool registered = false;
    if (!registered) {
        ProviderRegistry::instance().register_provider(
            std::make_shared<LocalProvider>(
                std::vector<std::string>{"hello"}, /*delay_ms=*/10));
        registered = true;
    }

    Model m;
    m.id = "multi-model";
    m.provider = "scripted-multi";
    m.api = ApiKind::OpenAICompletions;

    pi::agent::AgentLoopConfig cfg;
    cfg.model = m;
    cfg.tools = {};
    cfg.stream_opts.api_key = "fake";

    // First turn: 1 user message → expect 1 assistant response.
    std::vector<pi::ai::Message> history1;
    {
        pi::ai::UserMessage um;
        um.content.push_back(TextContent{"first prompt"});
        history1.push_back(um);
    }
    auto stream1 = pi::agent::run_agent_loop(history1, cfg);
    std::vector<pi::ai::Message> captured;
    stream1->drain([&](const pi::agent::AgentEvent& e) {
        if (e.kind == pi::agent::AgentEvent::Kind::AgentEnd) {
            captured = e.messages;
        }
    });

    REQUIRE(captured.size() >= 2);  // 1 user + 1 assistant
    CHECK(std::holds_alternative<pi::ai::UserMessage>(captured.front()));
    CHECK(std::holds_alternative<pi::ai::AssistantMessage>(captured.back()));

    // Second turn: append new user message, run again.
    pi::ai::UserMessage um2;
    um2.content.push_back(TextContent{"second prompt"});
    captured.push_back(um2);

    auto stream2 = pi::agent::run_agent_loop(captured, cfg);
    std::vector<pi::ai::Message> final2_msgs;
    stream2->drain([&](const pi::agent::AgentEvent& e) {
        if (e.kind == pi::agent::AgentEvent::Kind::AgentEnd) {
            final2_msgs = e.messages;
        }
    });

    // After 2 turns we expect at least: 2 user + 2 assistant messages.
    CHECK(final2_msgs.size() >= 4);

    // First message is still the first user prompt (history preserved).
    auto first = std::get<pi::ai::UserMessage>(final2_msgs.front());
    REQUIRE(!first.content.empty());
    CHECK(std::get<pi::ai::TextContent>(first.content[0]).text == "first prompt");

    // The last user message is the second prompt.
    auto last_user = std::get<pi::ai::UserMessage>(final2_msgs.at(final2_msgs.size() - 2));
    REQUIRE(!last_user.content.empty());
    CHECK(std::get<pi::ai::TextContent>(last_user.content[0]).text == "second prompt");
}

