// libs/agent/src/agent_loop.cpp
#include "pi_agent/agent_loop.hpp"

#include "pi_core/log.hpp"

#include <chrono>
#include <thread>
#include <utility>

namespace pi::agent {

namespace {

std::vector<pi::ai::Message> default_convert(const std::vector<pi::ai::Message>& in) {
    return in;
}

}  // namespace

std::shared_ptr<AgentEventStream> run_agent_loop(
    std::vector<pi::ai::Message> initial_messages,
    AgentLoopConfig config) {

    auto out = std::make_shared<AgentEventStream>();
    auto signal = config.signal;
    if (!signal) signal = std::make_shared<NullAbort>();

    std::thread([out, initial_messages = std::move(initial_messages),
                 config = std::move(config), signal]() mutable {
        std::vector<pi::ai::Message> messages = std::move(initial_messages);
        auto convert = config.convert_to_llm ? config.convert_to_llm : default_convert;

        // Build tool map by name.
        std::map<std::string, ToolPtr> tool_map;
        std::vector<pi::ai::ToolSpec> tool_specs;
        for (auto& t : config.tools) {
            tool_map[t->name()] = t;
            tool_specs.push_back(t->to_spec());
        }

        out->push(AgentEvent::agent_start());

        while (true) {
            if (signal->aborted()) {
                PI_LOG_INFO << "agent_loop: signal aborted, ending";
                break;
            }

            out->push(AgentEvent::turn_start());

            // Build LLM context.
            auto llm_messages = convert(messages);
            pi::ai::Context ctx;
            ctx.messages = llm_messages;
            ctx.tools = tool_specs;

            // Stream one turn.
            std::shared_ptr<pi::ai::AssistantMessage> final_msg;
            {
                auto sub = pi::ai::stream_simple(config.model, ctx, config.stream_opts);
                // Use blocking pull() so we don't race past events pushed by
                // the provider's detached worker thread.
                while (auto ev = sub->pull()) {
                    pi::ai::Message m;
                    m = ev->partial;
                    out->push(AgentEvent::message_update(std::move(m), *ev));
                    if (ev->kind == pi::ai::AssistantMessageEvent::Kind::Done
                        || ev->kind == pi::ai::AssistantMessageEvent::Kind::Error) {
                        break;
                    }
                }
                auto res = sub->result();
                if (!res) {
                    out->push(AgentEvent::message_end(pi::ai::Message{pi::ai::AssistantMessage{}}));
                    out->push(AgentEvent::agent_end(messages));
                    out->end();
                    return;
                }
                final_msg = std::make_shared<pi::ai::AssistantMessage>(std::move(res.value()));
            }

            // Record the assistant message.
            pi::ai::Message am{*final_msg};
            messages.push_back(am);
            out->push(AgentEvent::message_end(am));

            // If no tool calls, the turn is done.
            std::vector<pi::ai::ToolCall> tool_calls;
            for (auto& c : final_msg->content) {
                if (std::holds_alternative<pi::ai::ToolCall>(c)) {
                    tool_calls.push_back(std::get<pi::ai::ToolCall>(c));
                }
            }

            if (tool_calls.empty() || final_msg->stop_reason != "toolUse") {
                out->push(AgentEvent::turn_end(am, {}));
                out->push(AgentEvent::agent_end(messages));
                out->end();
                return;
            }

            // Execute tool calls in order.
            std::vector<pi::ai::ToolResultMessage> turn_tool_results;
            for (auto& tc : tool_calls) {
                if (signal->aborted()) break;

                pi::core::Json args;
                try {
                    args = pi::core::Json::parse(tc.arguments_json);
                } catch (...) {
                    args = pi::core::Json::object();
                }

                out->push(AgentEvent::tool_start(tc.id, tc.name, args));

                ToolResult result;
                bool is_error = false;
                auto it = tool_map.find(tc.name);
                if (it == tool_map.end()) {
                    pi::ai::TextContent err_text;
                    err_text.text = "Error: tool not found: " + tc.name;
                    result.content.push_back(err_text);
                    is_error = true;
                } else {
                    try {
                        result = it->second->execute(args, *signal,
                                                    [](const pi::ai::Content&) {});
                    } catch (const std::exception& e) {
                        pi::ai::TextContent err_text;
                        err_text.text = std::string("Error: ") + e.what();
                        result.content = {err_text};
                        is_error = true;
                    }
                }

                out->push(AgentEvent::tool_end(tc.id, tc.name, result, is_error));

                pi::ai::ToolResultMessage tr;
                tr.tool_call_id = tc.id;
                tr.tool_name = tc.name;
                tr.content = result.content;
                tr.details = result.details;
                tr.is_error = is_error;
                tr.timestamp = static_cast<int64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count());
                turn_tool_results.push_back(tr);

                pi::ai::Message tr_msg = tr;
                messages.push_back(tr_msg);
                out->push(AgentEvent::message_end(tr_msg));
            }

            out->push(AgentEvent::turn_end(am, turn_tool_results));
        }

        out->push(AgentEvent::agent_end(messages));
        out->end();
    }).detach();

    return out;
}

// ---------------------------------------------------------------------------
// agent_loop_continue: continue from existing state.messages.
// Caller must have set up state.messages via prior run_agent_loop and
// accumulated final messages. The last message must be user or toolResult.
// ---------------------------------------------------------------------------

std::shared_ptr<AgentEventStream> run_agent_loop_continue(
    AgentLoopConfig config) {

    // The caller is responsible for passing in `messages` via config; here
    // we expect config to have a pre-populated `messages` member. Since
    // AgentLoopConfig doesn't carry messages yet, the caller (e.g.
    // AgentSession) is responsible for holding them and re-injecting
    // here. V1 simplification: we accept an empty initial_messages and
    // require caller to push history onto a side channel before calling.
    //
    // For V1 interactive mode, the simplest correct pattern is to call
    // run_agent_loop() with the full history as `initial_messages`, then
    // (for next turn) reuse `event.messages` from agent_end. We don't
    // actually need a separate _continue entry point — it's a V2 helper.

    auto out = std::make_shared<AgentEventStream>();
    // For now, signal that this entry point is not yet wired.
    pi::ai::AssistantMessage m;
    m.stop_reason = "error";
    m.error_message = "run_agent_loop_continue: not yet implemented; use run_agent_loop with full history";
    out->push(AgentEvent::agent_start());
    out->push(AgentEvent::message_end(pi::ai::Message{m}));
    out->push(AgentEvent::agent_end({}));
    out->end();
    return out;
}

}  // namespace pi::agent
