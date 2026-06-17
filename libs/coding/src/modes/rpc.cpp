// libs/coding/src/modes/rpc.cpp
#include "pi_coding/modes/rpc.hpp"

#include "pi_agent/agent_loop.hpp"
#include "pi_core/json.hpp"
#include "pi_coding/tools/bash_tool.hpp"
#include "pi_coding/tools/edit_tool.hpp"
#include "pi_coding/tools/read_tool.hpp"
#include "pi_coding/tools/write_tool.hpp"

#include <atomic>
#include <csignal>
#include <iostream>
#include <sstream>
#include <thread>

namespace pi::coding::modes {

namespace {

std::atomic<bool> g_done{false};
void on_signal(int) { g_done.store(true); }

void write_event(const pi::core::Json& j) {
    std::cout << j.dump() << "\n";
    std::cout.flush();
}

void write_response(const std::string& id, const std::string& command,
                    bool ok, const pi::core::Json& data_or_error) {
    pi::core::Json j;
    j["type"] = "response";
    if (!id.empty()) j["id"] = id;
    j["command"] = command;
    j["success"] = ok;
    if (ok) {
        if (!data_or_error.is_null()) j["data"] = data_or_error;
    } else {
        j["error"] = data_or_error.is_null() ? "" : data_or_error;
    }
    write_event(j);
}

pi::core::Json message_to_json(const pi::ai::Message& m) {
    return std::visit([](auto& v) -> pi::core::Json {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, pi::ai::UserMessage>) {
            return v.to_json();
        } else if constexpr (std::is_same_v<T, pi::ai::AssistantMessage>) {
            return v.to_json();
        } else {
            // ToolResultMessage
            pi::core::Json j;
            j["role"] = "toolResult";
            j["toolCallId"] = v.tool_call_id;
            j["toolName"] = v.tool_name;
            j["isError"] = v.is_error;
            pi::core::Json arr = pi::core::Json::array();
            for (auto& c : v.content) {
                if (std::holds_alternative<pi::ai::TextContent>(c)) {
                    arr.push_back({{"type", "text"},
                                   {"text", std::get<pi::ai::TextContent>(c).text}});
                }
            }
            j["content"] = arr;
            return j;
        }
    }, m);
}

void forward_agent_event(const pi::agent::AgentEvent& ev) {
    pi::core::Json j;
    j["type"] = pi::agent::to_string(ev.kind);
    if (ev.kind == pi::agent::AgentEvent::Kind::MessageUpdate) {
        auto& aev = ev.assistant_event;
        j["event"] = pi::ai::to_string(aev.kind);
        if (aev.kind == pi::ai::AssistantMessageEvent::Kind::TextDelta) {
            j["delta"] = aev.delta;
        } else if (aev.kind == pi::ai::AssistantMessageEvent::Kind::ToolCallEnd) {
            j["toolCall"] = {{"id", aev.tool_call.id}, {"name", aev.tool_call.name}};
        }
    } else if (ev.kind == pi::agent::AgentEvent::Kind::MessageEnd) {
        j["message"] = message_to_json(ev.message);
    } else if (ev.kind == pi::agent::AgentEvent::Kind::ToolExecutionStart) {
        j["tool"] = ev.tool_name;
        j["args"] = ev.tool_args;
    } else if (ev.kind == pi::agent::AgentEvent::Kind::ToolExecutionEnd) {
        j["tool"] = ev.tool_name;
        j["isError"] = ev.tool_is_error;
        j["details"] = ev.tool_result.details;
        pi::core::Json content = pi::core::Json::array();
        for (auto& c : ev.tool_result.content) {
            if (std::holds_alternative<pi::ai::TextContent>(c)) {
                content.push_back({{"type", "text"},
                                   {"text", std::get<pi::ai::TextContent>(c).text}});
            }
        }
        j["content"] = content;
    }
    write_event(j);
}

}  // namespace

int run_rpc_mode(const pi::ai::Model& model,
                 pi::ai::SimpleStreamOptions opts,
                 std::string cwd,
                 std::function<std::string(const std::string&)> /*api_key_resolver*/) {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    // Build the standard tools.
    auto tools_fn = [&]() {
        std::vector<pi::agent::ToolPtr> tools;
        tools.push_back(std::make_shared<pi::coding::tools::BashTool>());
        tools.push_back(std::make_shared<pi::coding::tools::ReadTool>(cwd));
        tools.push_back(std::make_shared<pi::coding::tools::WriteTool>(cwd));
        tools.push_back(std::make_shared<pi::coding::tools::EditTool>(cwd));
        return tools;
    };

    // Flush stdout after every line (line-buffered is default on tty but we're not on tty).
    std::cout.setf(std::ios::unitbuf);

    std::string line;
    while (!g_done && std::getline(std::cin, line)) {
        if (line.empty()) continue;
        auto j = pi::core::tryParse(line);
        if (!j) {
            pi::core::Json err;
            err["type"] = "response";
            err["command"] = "<parse>";
            err["success"] = false;
            err["error"] = "invalid JSON";
            write_event(err);
            continue;
        }
        std::string type = j->value("type", std::string{});
        std::string id = j->value("id", std::string{});

        if (type == "prompt") {
            std::string text = j->value("text", std::string{});

            // Build messages including any image attachments.
            std::vector<pi::ai::Message> messages;
            pi::ai::UserMessage um;
            um.content.push_back(pi::ai::TextContent{text});
            if (j->contains("images") && (*j)["images"].is_array()) {
                for (auto& im : (*j)["images"]) {
                    pi::ai::ImageContent img;
                    img.mime_type = im.value("mimeType", std::string{"image/png"});
                    img.data = im.value("data", std::string{});
                    um.content.push_back(img);
                }
            }
            messages.push_back(um);

            pi::agent::AgentLoopConfig cfg;
            cfg.model = model;
            cfg.tools = tools_fn();
            cfg.stream_opts = opts;

            auto stream = pi::agent::run_agent_loop(std::move(messages), std::move(cfg));
            stream->drain([&](const pi::agent::AgentEvent& ev) {
                forward_agent_event(ev);
            });
            write_response(id, "prompt", true, nullptr);
        } else if (type == "abort") {
            // No persistent agent session in V1; just ack.
            write_response(id, "abort", true, nullptr);
        } else if (type == "set_model") {
            std::string provider = j->value("provider", std::string{});
            std::string model_id = j->value("modelId", std::string{});
            // For V1 we just acknowledge; model is set at startup.
            write_response(id, "set_model", true,
                          {{"provider", provider}, {"modelId", model_id}});
        } else if (type == "get_state") {
            write_response(id, "get_state", true,
                          {{"provider", model.provider},
                           {"model", model.id}});
        } else if (type == "list_sessions") {
            write_response(id, "list_sessions", true,
                          {{"cwd", cwd}});  // V1: not implemented
        } else if (type == "ping") {
            write_response(id, "ping", true, {{"pong", true}});
        } else {
            write_response(id, type, false, "unknown command");
        }
    }
    return 0;
}

}  // namespace pi::coding::modes
