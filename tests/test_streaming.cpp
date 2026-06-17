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
#include "pi_ai/types.hpp"

#include <chrono>
#include <thread>
#include <vector>

using namespace pi::ai;

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
