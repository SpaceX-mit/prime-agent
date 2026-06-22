// libs/coding/src/modes/rpc.cpp
// RPC mode: line-delimited JSON over stdin/stdout.
// Stateful multi-turn agent session with session persistence, abort, model
// switching, compaction, and new_session. Mirrors upstream rpc-mode.ts.

#include "pi_coding/modes/rpc.hpp"

#include "pi_agent/agent_loop.hpp"
#include "pi_coding/compaction.hpp"
#include "pi_coding/context_files.hpp"
#include "pi_coding/session_manager.hpp"
#include "pi_coding/skills.hpp"
#include "pi_coding/tools/bash_tool.hpp"
#include "pi_coding/tools/edit_tool.hpp"
#include "pi_coding/tools/fetch_tool.hpp"
#include "pi_coding/tools/find_tool.hpp"
#include "pi_coding/tools/grep_tool.hpp"
#include "pi_coding/tools/ls_tool.hpp"
#include "pi_coding/tools/read_tool.hpp"
#include "pi_coding/tools/write_tool.hpp"
#include "pi_core/json.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>

namespace pi::coding::modes {

namespace {

// ---------------------------------------------------------------------------
// I/O helpers
// ---------------------------------------------------------------------------

std::atomic<bool> g_done{false};
void on_signal(int) { g_done.store(true); }

void write_event(const pi::core::Json& j) {
    std::cout << j.dump() << "\n";
    std::cout.flush();
}

void write_response(const std::string& id, const std::string& command,
                    bool ok, const pi::core::Json& payload) {
    pi::core::Json j;
    j["type"] = "response";
    if (!id.empty()) j["id"] = id;
    j["command"] = command;
    j["success"] = ok;
    if (ok)  { if (!payload.is_null()) j["data"]  = payload; }
    else     { j["error"] = payload.is_null() ? pi::core::Json("") : payload; }
    write_event(j);
}

pi::core::Json message_to_json(const pi::ai::Message& m) {
    return std::visit([](auto& v) -> pi::core::Json {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, pi::ai::UserMessage> ||
                      std::is_same_v<T, pi::ai::AssistantMessage>) {
            return v.to_json();
        } else {
            pi::core::Json j;
            j["role"] = "toolResult";
            j["toolCallId"] = v.tool_call_id;
            j["toolName"] = v.tool_name;
            j["isError"] = v.is_error;
            pi::core::Json arr = pi::core::Json::array();
            for (auto& c : v.content)
                if (std::holds_alternative<pi::ai::TextContent>(c))
                    arr.push_back({{"type", "text"},
                                   {"text", std::get<pi::ai::TextContent>(c).text}});
            j["content"] = arr;
            return j;
        }
    }, m);
}

void forward_agent_event(const pi::agent::AgentEvent& ev) {
    pi::core::Json j;
    j["type"] = pi::agent::to_string(ev.kind);
    switch (ev.kind) {
        case pi::agent::AgentEvent::Kind::MessageUpdate: {
            auto& aev = ev.assistant_event;
            j["event"] = pi::ai::to_string(aev.kind);
            if (aev.kind == pi::ai::AssistantMessageEvent::Kind::TextDelta)
                j["delta"] = aev.delta;
            else if (aev.kind == pi::ai::AssistantMessageEvent::Kind::ThinkingDelta)
                j["delta"] = aev.delta;
            else if (aev.kind == pi::ai::AssistantMessageEvent::Kind::ToolCallEnd)
                j["toolCall"] = {{"id", aev.tool_call.id}, {"name", aev.tool_call.name}};
            break;
        }
        case pi::agent::AgentEvent::Kind::MessageEnd:
            j["message"] = message_to_json(ev.message);
            break;
        case pi::agent::AgentEvent::Kind::ToolExecutionStart:
            j["tool"] = ev.tool_name;
            j["args"] = ev.tool_args;
            break;
        case pi::agent::AgentEvent::Kind::ToolExecutionEnd: {
            j["tool"] = ev.tool_name;
            j["isError"] = ev.tool_is_error;
            pi::core::Json content = pi::core::Json::array();
            for (auto& c : ev.tool_result.content)
                if (std::holds_alternative<pi::ai::TextContent>(c))
                    content.push_back({{"type", "text"},
                                       {"text", std::get<pi::ai::TextContent>(c).text}});
            j["content"] = content;
            break;
        }
        default: break;
    }
    write_event(j);
}

// ---------------------------------------------------------------------------
// Session state
// ---------------------------------------------------------------------------

struct RpcSession {
    pi::ai::Model model;
    pi::ai::SimpleStreamOptions opts;
    std::string cwd;
    std::function<std::string(const std::string&)> key_resolver;

    std::vector<pi::ai::Message> history;
    std::string system_prompt;
    std::unique_ptr<pi::coding::SessionManager> session;

    // Current in-flight abort signal (null when idle).
    std::shared_ptr<pi::agent::MutableAbort> abort_signal;
    std::mutex abort_mtx;
    bool streaming = false;

    void init_session() {
        std::string path = pi::coding::SessionManager::default_dir() + "/" +
                           pi::coding::SessionManager::new_session_id() + ".jsonl";
        pi::coding::SessionManager sm(path);
        pi::coding::SessionHeader hdr;
        auto slash = sm.path().find_last_of('/');
        hdr.id = sm.path().substr(slash + 1, sm.path().size() - slash - 6 - 1);
        std::time_t t = std::time(nullptr);
        std::tm tm{}; gmtime_r(&t, &tm);
        char buf[32]; std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
        hdr.timestamp = buf;
        hdr.cwd = cwd;
        sm.initialize(hdr);
        session = std::make_unique<pi::coding::SessionManager>(path);
    }

    void persist_message(const pi::ai::Message& m) {
        if (!session) return;
        pi::coding::SessionEntry e;
        e.type = "message";
        e.data["message"] = message_to_json(m);
        session->append_entry(e);
    }

    std::vector<pi::agent::ToolPtr> tools() const {
        return {
            std::make_shared<pi::coding::tools::BashTool>(),
            std::make_shared<pi::coding::tools::ReadTool>(cwd),
            std::make_shared<pi::coding::tools::WriteTool>(cwd),
            std::make_shared<pi::coding::tools::EditTool>(cwd),
            std::make_shared<pi::coding::tools::GrepTool>(cwd),
            std::make_shared<pi::coding::tools::FindTool>(cwd),
            std::make_shared<pi::coding::tools::LsTool>(cwd),
            std::make_shared<pi::coding::tools::FetchTool>(),
        };
    }

    pi::core::Json state_json() const {
        return {
            {"provider", model.provider},
            {"model", model.id},
            {"cwd", cwd},
            {"messageCount", static_cast<int>(history.size())},
            {"sessionPath", session ? session->path() : ""},
        };
    }
};

// ---------------------------------------------------------------------------
// Command handlers
// ---------------------------------------------------------------------------

void handle_prompt(const pi::core::Json& j, const std::string& id, RpcSession& s) {
    std::string text = j.value("text", std::string{});
    pi::ai::UserMessage um;
    um.content.push_back(pi::ai::TextContent{text});
    // Image attachments.
    if (j.contains("images") && j["images"].is_array()) {
        for (auto& im : j["images"]) {
            pi::ai::ImageContent img;
            img.mime_type = im.value("mimeType", std::string{"image/png"});
            img.data = im.value("data", std::string{});
            um.content.push_back(img);
        }
    }
    s.history.push_back(um);
    s.persist_message(s.history.back());

    auto abort_signal = std::make_shared<pi::agent::MutableAbort>();
    {
        std::lock_guard<std::mutex> g(s.abort_mtx);
        s.abort_signal = abort_signal;
        s.streaming = true;
    }

    pi::agent::AgentLoopConfig cfg;
    cfg.model = s.model;
    cfg.tools = s.tools();
    cfg.stream_opts = s.opts;
    cfg.system_prompt = s.system_prompt;
    cfg.signal = abort_signal;

    auto stream = pi::agent::run_agent_loop(s.history, std::move(cfg));
    std::vector<pi::ai::Message> final_msgs;
    stream->drain([&](const pi::agent::AgentEvent& ev) {
        forward_agent_event(ev);
        if (ev.kind == pi::agent::AgentEvent::Kind::MessageEnd)
            s.persist_message(ev.message);
        if (ev.kind == pi::agent::AgentEvent::Kind::AgentEnd)
            final_msgs = ev.messages;
    });
    if (!final_msgs.empty()) s.history = final_msgs;

    {
        std::lock_guard<std::mutex> g(s.abort_mtx);
        s.abort_signal.reset();
        s.streaming = false;
    }
    write_response(id, "prompt", true, nullptr);
}

void handle_abort(const std::string& id, RpcSession& s) {
    std::lock_guard<std::mutex> g(s.abort_mtx);
    if (s.abort_signal) s.abort_signal->signal();
    write_response(id, "abort", true, {{"wasStreaming", s.streaming}});
}

void handle_set_model(const pi::core::Json& j, const std::string& id, RpcSession& s) {
    std::string model_id = j.value("modelId", std::string{});
    std::string provider = j.value("provider", std::string{});
    std::string key = model_id.empty() ? "" : s.key_resolver ? s.key_resolver(
        provider.empty() ? model_id : provider) : "";
    std::string full_id = (!provider.empty() && model_id.find('/') == std::string::npos)
        ? provider + "/" + model_id : model_id;
    const pi::ai::Model* m = pi::ai::find_model(full_id);
    if (!m) {
        write_response(id, "set_model", false, "unknown model: " + full_id);
        return;
    }
    if (key.empty() && s.key_resolver) key = s.key_resolver(m->provider);
    if (key.empty()) {
        write_response(id, "set_model", false, "no API key for provider: " + m->provider);
        return;
    }
    s.model = *m;
    s.opts.api_key = key;
    write_response(id, "set_model", true, {{"provider", m->provider}, {"modelId", m->id}});
}

void handle_compact(const std::string& id, RpcSession& s) {
    if (s.history.empty()) {
        write_response(id, "compact", true, {{"droppedCount", 0}});
        return;
    }
    pi::coding::CompactionSettings cs;
    if (s.model.context_window > 0) cs.target_context = s.model.context_window;
    auto res = pi::coding::compact(s.model, s.opts, s.history, cs);
    if (res.aborted) { write_response(id, "compact", false, "compaction failed"); return; }
    s.history = res.kept_messages;
    if (s.session) {
        pi::coding::SessionEntry e;
        e.type = "compaction";
        e.data["summary"] = res.summary;
        e.data["droppedCount"] = res.drop_count;
        s.session->append_entry(e);
    }
    write_response(id, "compact", true, {{"droppedCount", res.drop_count},
                                          {"summary", res.summary}});
}

void handle_new_session(const std::string& id, RpcSession& s) {
    s.history.clear();
    s.init_session();
    write_response(id, "new_session", true, s.state_json());
}

void handle_list_sessions(const std::string& id, RpcSession& s) {
    auto all = pi::coding::SessionManager::list_all();
    pi::core::Json arr = pi::core::Json::array();
    for (auto& si : all) {
        arr.push_back({{"id", si.id}, {"path", si.path},
                       {"timestamp", si.timestamp}, {"cwd", si.cwd},
                       {"messageCount", si.message_count}});
    }
    (void)s;
    write_response(id, "list_sessions", true, {{"sessions", arr}});
}

void handle_get_session(const std::string& id, RpcSession& s) {
    write_response(id, "get_session", true, s.state_json());
}

}  // namespace

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int run_rpc_mode(const pi::ai::Model& model,
                 pi::ai::SimpleStreamOptions opts,
                 std::string cwd,
                 std::function<std::string(const std::string&)> api_key_resolver) {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    std::cout.setf(std::ios::unitbuf);

    RpcSession s;
    s.model = model;
    s.opts  = opts;
    s.cwd   = cwd;
    s.key_resolver = api_key_resolver;
    s.init_session();

    // Build system prompt with context files + skills.
    {
        auto ctxf = pi::coding::load_project_context_files(cwd);
        s.system_prompt = pi::coding::build_system_prompt(cwd, ctxf);
        s.system_prompt += pi::coding::format_skills_for_prompt(
            pi::coding::load_skills(cwd));
    }

    // Announce ready.
    write_event({{"type", "ready"}, {"session", s.state_json()}});

    std::string line;
    while (!g_done && std::getline(std::cin, line)) {
        if (line.empty()) continue;
        auto j = pi::core::tryParse(line);
        if (!j) {
            write_response("", "<parse>", false, "invalid JSON");
            continue;
        }
        std::string type = j->value("type", std::string{});
        std::string id   = j->value("id",   std::string{});

        if      (type == "prompt")       handle_prompt(*j, id, s);
        else if (type == "abort")        handle_abort(id, s);
        else if (type == "set_model")    handle_set_model(*j, id, s);
        else if (type == "compact")      handle_compact(id, s);
        else if (type == "new_session")  handle_new_session(id, s);
        else if (type == "list_sessions") handle_list_sessions(id, s);
        else if (type == "get_session")  handle_get_session(id, s);
        else if (type == "get_state")    write_response(id, "get_state", true, s.state_json());
        else if (type == "ping")         write_response(id, "ping", true, {{"pong", true}});
        else write_response(id, type, false, "unknown command: " + type);
    }
    return 0;
}

}  // namespace pi::coding::modes
