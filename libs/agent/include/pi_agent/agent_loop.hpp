// libs/agent/include/pi_agent/agent_loop.hpp
// The agent event loop.
//
// Mirrors the TS `agentLoop` function. Runs a multi-turn loop until the LLM
// returns a stop reason other than "toolUse", or the signal is aborted.
//
// All events are pushed to a shared EventStream. The caller drains them
// (either with a callback or via pull).

#pragma once

#include "pi_ai/event_stream.hpp"
#include "pi_ai/stream_simple.hpp"
#include "pi_ai/types.hpp"
#include "tool.hpp"

#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace pi::agent {

// ---------------------------------------------------------------------------
// Event types
// ---------------------------------------------------------------------------

struct AgentEvent {
    enum class Kind {
        AgentStart,
        AgentEnd,
        TurnStart,
        TurnEnd,
        MessageStart,
        MessageUpdate,
        MessageEnd,
        ToolExecutionStart,
        ToolExecutionUpdate,
        ToolExecutionEnd,
    };
    Kind kind = Kind::AgentStart;

    pi::ai::Message message;
    pi::ai::AssistantMessageEvent assistant_event;
    pi::ai::ToolCall tool_call;
    std::string tool_call_id;
    std::string tool_name;
    pi::core::Json tool_args = pi::core::Json::object();
    ToolResult tool_result;
    bool tool_is_error = false;
    pi::ai::Message assistant_message_snap;
    std::vector<pi::ai::ToolResultMessage> tool_results;

    static AgentEvent agent_start() { AgentEvent e; e.kind = Kind::AgentStart; return e; }
    static AgentEvent agent_end(std::vector<pi::ai::Message> messages) {
        AgentEvent e; e.kind = Kind::AgentEnd; return e;
    }
    static AgentEvent turn_start() { AgentEvent e; e.kind = Kind::TurnStart; return e; }
    static AgentEvent turn_end(pi::ai::Message m, std::vector<pi::ai::ToolResultMessage> tr) {
        AgentEvent e; e.kind = Kind::TurnEnd;
        e.message = std::move(m);
        e.tool_results = std::move(tr);
        return e;
    }
    static AgentEvent message_start(pi::ai::Message m) {
        AgentEvent e; e.kind = Kind::MessageStart; e.message = std::move(m); return e;
    }
    static AgentEvent message_update(pi::ai::Message m, pi::ai::AssistantMessageEvent a) {
        AgentEvent e; e.kind = Kind::MessageUpdate; e.message = std::move(m); e.assistant_event = std::move(a); return e;
    }
    static AgentEvent message_end(pi::ai::Message m) {
        AgentEvent e; e.kind = Kind::MessageEnd; e.message = std::move(m); return e;
    }
    static AgentEvent tool_start(std::string id, std::string n, pi::core::Json args) {
        AgentEvent e; e.kind = Kind::ToolExecutionStart;
        e.tool_call_id = std::move(id); e.tool_name = std::move(n);
        e.tool_args = std::move(args);
        return e;
    }
    static AgentEvent tool_end(std::string id, std::string n, ToolResult r, bool err) {
        AgentEvent e; e.kind = Kind::ToolExecutionEnd;
        e.tool_call_id = std::move(id); e.tool_name = std::move(n);
        e.tool_result = std::move(r); e.tool_is_error = err;
        return e;
    }
};

inline const char* to_string(AgentEvent::Kind k) {
    switch (k) {
        case AgentEvent::Kind::AgentStart:         return "agent_start";
        case AgentEvent::Kind::AgentEnd:           return "agent_end";
        case AgentEvent::Kind::TurnStart:          return "turn_start";
        case AgentEvent::Kind::TurnEnd:            return "turn_end";
        case AgentEvent::Kind::MessageStart:       return "message_start";
        case AgentEvent::Kind::MessageUpdate:      return "message_update";
        case AgentEvent::Kind::MessageEnd:         return "message_end";
        case AgentEvent::Kind::ToolExecutionStart: return "tool_execution_start";
        case AgentEvent::Kind::ToolExecutionUpdate:return "tool_execution_update";
        case AgentEvent::Kind::ToolExecutionEnd:   return "tool_execution_end";
    }
    return "?";
}

/// Event stream specialized for AgentEvent.
class AgentEventStream {
public:
    using Event = AgentEvent;
    using Callback = std::function<void(const Event&)>;

    AgentEventStream() = default;

    void push(Event e) {
        std::lock_guard<std::mutex> g(mu_);
        queue_.push_back(std::move(e));
        cv_.notify_one();
    }
    void end() {
        {
            std::lock_guard<std::mutex> g(mu_);
            finished_ = true;
        }
        cv_.notify_all();
    }

    /// Drain all events with the callback (blocks until end() is called).
    void drain(Callback cb) {
        while (true) {
            std::unique_lock<std::mutex> g(mu_);
            cv_.wait(g, [&] { return !queue_.empty() || finished_; });
            while (!queue_.empty()) {
                auto e = std::move(queue_.front());
                queue_.pop_front();
                g.unlock();
                if (cb) cb(e);
                g.lock();
            }
            if (finished_) break;
        }
    }

private:
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<Event> queue_;
    bool finished_ = false;
};

// ---------------------------------------------------------------------------
// Loop configuration
// ---------------------------------------------------------------------------

/// Convert agent-level messages into LLM-ready messages.
/// Default behavior: pass-through (UserMessage -> UserMessage, etc.).
using ConvertFn = std::function<std::vector<pi::ai::Message>(const std::vector<pi::ai::Message>&)>;

struct AgentLoopConfig {
    pi::ai::Model model;
    std::vector<ToolPtr> tools;
    ConvertFn convert_to_llm;     // optional; defaults to identity

    pi::ai::SimpleStreamOptions stream_opts;
    std::shared_ptr<AbortSignal> signal;
};

// ---------------------------------------------------------------------------
// Loop result
// ---------------------------------------------------------------------------

struct AgentLoopResult {
    std::vector<pi::ai::Message> messages;
};

// ---------------------------------------------------------------------------
// The loop function — runs in a background thread.
// Pushes events into the returned AgentEventStream; the final message is
// the AssistantMessage returned by the last LLM turn.
// ---------------------------------------------------------------------------

std::shared_ptr<AgentEventStream> run_agent_loop(
    std::vector<pi::ai::Message> initial_messages,
    AgentLoopConfig config);

}  // namespace pi::agent
