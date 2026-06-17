// libs/ai/include/pi_ai/event_stream.hpp
// Callback-based stream. The TS version is AsyncIterable-based; in C++ we
// push events into a buffer and let consumers pull, or use a callback.

#pragma once

#include "pi_core/error.hpp"
#include "pi_core/result.hpp"
#include "types.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace pi::ai {

/// An event sink that consumers can read from synchronously or via callback.
/// All methods are thread-safe.
class EventStream {
public:
    using Event = AssistantMessageEvent;
    using EventCallback = std::function<void(const Event&)>;

    EventStream() = default;
    ~EventStream() = default;

    /// Push an event; non-blocking. Wakes any blocked readers.
    void push(Event e);

    /// Mark the stream as ended with a final result (AssistantMessage).
    void end(AssistantMessage final_message);
    /// Mark as errored.
    void end_with_error(pi::core::Error err);

    /// True if end() or end_with_error() has been called.
    bool finished() const;

    /// Block until an event is available or stream ends; returns nullopt on EOF.
    std::optional<Event> pull();

    /// Try to pull without blocking.
    std::optional<Event> try_pull();

    /// Pull all remaining events and the final message. Used for one-shot consumption.
    pi::core::Result<AssistantMessage> drain_to_completion(EventCallback cb = nullptr);

    /// Get the final message (blocks until end).
    pi::core::Result<AssistantMessage> result();

    const pi::core::Error& error() const { return err_; }

private:
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<Event> queue_;
    std::optional<AssistantMessage> final_;
    bool finished_ = false;
    pi::core::Error err_;
};

}  // namespace pi::ai
