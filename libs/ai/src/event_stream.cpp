// libs/ai/src/event_stream.cpp
#include "pi_ai/event_stream.hpp"

#include <utility>

namespace pi::ai {

void EventStream::push(Event e) {
    std::lock_guard<std::mutex> g(mu_);
    queue_.push_back(std::move(e));
    cv_.notify_one();
}

void EventStream::end(AssistantMessage final_message) {
    {
        std::lock_guard<std::mutex> g(mu_);
        final_ = std::move(final_message);
        finished_ = true;
    }
    cv_.notify_all();
}

void EventStream::end_with_error(pi::core::Error err) {
    {
        std::lock_guard<std::mutex> g(mu_);
        err_ = std::move(err);
        finished_ = true;
    }
    cv_.notify_all();
}

bool EventStream::finished() const {
    std::lock_guard<std::mutex> g(mu_);
    return finished_;
}

std::optional<EventStream::Event> EventStream::pull() {
    std::unique_lock<std::mutex> g(mu_);
    cv_.wait(g, [&] { return !queue_.empty() || finished_; });
    if (queue_.empty()) return std::nullopt;
    Event e = std::move(queue_.front());
    queue_.pop_front();
    return e;
}

std::optional<EventStream::Event> EventStream::try_pull() {
    std::lock_guard<std::mutex> g(mu_);
    if (queue_.empty()) return std::nullopt;
    Event e = std::move(queue_.front());
    queue_.pop_front();
    return e;
}

pi::core::Result<AssistantMessage> EventStream::drain_to_completion(EventCallback cb) {
    while (auto e = pull()) {
        if (cb) cb(*e);
    }
    if (err_.ok()) {
        if (!final_) {
            return pi::core::make_error(pi::core::ErrorKind::Internal,
                                        "ai: stream ended without result");
        }
        return *final_;
    }
    // Error: return a synthetic error AssistantMessage if final is missing.
    if (!final_) {
        AssistantMessage m;
        m.stop_reason = "error";
        m.error_message = err_.to_string();
        return m;
    }
    return *final_;
}

pi::core::Result<AssistantMessage> EventStream::result() {
    std::unique_lock<std::mutex> g(mu_);
    cv_.wait(g, [&] { return finished_; });
    if (err_.ok()) {
        if (!final_) {
            return pi::core::make_error(pi::core::ErrorKind::Internal,
                                        "ai: stream ended without result");
        }
        return *final_;
    }
    return pi::core::Error{err_};
}

}  // namespace pi::ai
