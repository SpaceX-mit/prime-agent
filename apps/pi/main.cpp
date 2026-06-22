// apps/pi/main.cpp
// Phase 0-2 entry point.
// Supports:
//   --help / -h, --version / -V, --list-models
//   -p <prompt>             Print mode (single-shot, agent + tools)
//   --model <id>            Model id
//   --provider <name>       Provider override
//   --api-key <key>         Override API key
//   --max-tokens <n>
//   --temperature <f>
//   --json                  Emit JSON events (text deltas + tool events)
//
// Phase 2: -p routes through AgentLoop with bash/read/write/edit tools.

#include "pi_agent/agent_loop.hpp"
#include "pi_ai/models.hpp"
#include "pi_ai/provider.hpp"
#include "pi_ai/stream_simple.hpp"
#include "pi_ai/types.hpp"
#include "pi_coding/auth_storage.hpp"
#include "pi_coding/compaction.hpp"
#include "pi_coding/context_files.hpp"
#include "pi_coding/html_export.hpp"
#include "pi_coding/modes/rpc.hpp"
#include "pi_coding/session_manager.hpp"
#include "pi_coding/settings_manager.hpp"
#include "pi_coding/skills.hpp"
#include "pi_coding/tools/bash_tool.hpp"
#include "pi_coding/tools/edit_tool.hpp"
#include "pi_coding/tools/fetch_tool.hpp"
#include "pi_coding/tools/web_search_tool.hpp"
#include "pi_coding/tools/read_tool.hpp"
#include "pi_coding/tools/write_tool.hpp"
#include "pi_core/ansi.hpp"
#include "pi_core/env.hpp"
#include "pi_core/json.hpp"
#include "pi_core/log.hpp"
#include "pi_core/path.hpp"
#include "pi_core/strutil.hpp"
#include "pi_tui/modes/interactive.hpp"
#include "pi_tui/terminal.hpp"

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

using namespace pi;

namespace {

const char* kUsage = R"(pi — prime-agent (C/C++ reimplementation)

USAGE:
  pi [options]                Interactive mode (not yet implemented)
  pi -p "<prompt>"            One-shot prompt (print mode, agent + tools)

OPTIONS:
  -h, --help                  Show this help
  -V, --version               Print version
      --list-models           List all built-in models (json)
      --model <id>            Model id (e.g. anthropic/claude-sonnet-4-5)
      --provider <name>       Provider name (e.g. anthropic)
      --api-key <key>         Override API key
      --max-tokens <n>        Limit output tokens
      --temperature <f>       Sampling temperature
  -c, --continue              Continue the most recent session
  -r, --resume                Pick a session to resume
      --session <id>          Open a specific session (8+ char prefix)
      --export <path>         Export a session to HTML (V2)
  -p <text>                   Print mode (agent loop with bash/read/write/edit)
      --json                  Emit JSON events instead of plain text
      --no-context-files, -nc Disable AGENTS.md / CLAUDE.md discovery + loading
      --no-skills             Disable skill discovery and loading
      --mode <mode>           One of: interactive (default), rpc, json
      --list-sessions         List all sessions (JSON)

ENVIRONMENT:
  ANTHROPIC_API_KEY           Anthropic API key
  OPENAI_API_KEY              OpenAI API key

EXAMPLES:
  pi -p "say exactly: ok"
  pi -p "what's 2+2" --model openai/gpt-4o-mini
  pi --list-models
)";

std::atomic<bool> g_interrupted{false};

void on_signal(int) { g_interrupted.store(true); }

void print_models_json() {
    auto j = core::Json::array();
    for (auto& m : ai::builtin_models()) {
        j.push_back({
            {"id", m.id},
            {"name", m.name},
            {"provider", m.provider},
            {"api", ai::to_string(m.api)},
            {"baseUrl", m.base_url},
            {"reasoning", m.reasoning},
            {"contextWindow", m.context_window},
            {"maxTokens", m.max_tokens},
            {"input", m.input},
            {"cost", {{"input", m.cost.input},
                      {"output", m.cost.output},
                      {"cacheRead", m.cost.cache_read},
                      {"cacheWrite", m.cost.cache_write}}},
        });
    }
    std::cout << j.dump(2) << std::endl;
}

std::string resolve_api_key(const std::string& provider) {
    // Priority: PRIME_AGENT_API_KEY > provider-specific env > auth.json > settings.json
    if (auto k = core::env::get("PRIME_AGENT_API_KEY"); k && !k->empty()) return *k;

    if (provider == "anthropic") {
        if (auto k = core::env::get("ANTHROPIC_API_KEY"); k && !k->empty()) return *k;
    } else if (provider == "openai") {
        if (auto k = core::env::get("OPENAI_API_KEY"); k && !k->empty()) return *k;
    } else if (provider == "minimax") {
        if (auto k = core::env::get("MINIMAX_API_KEY"); k && !k->empty()) return *k;
    }

    // auth.json
    if (auto h = core::path::home_dir(); h) {
        coding::AuthStorage auth(*h + "/.pi/agent/auth.json");
        if (auto c = auth.get(provider); c.has_value()
            && c->type == coding::AuthCredential::Type::ApiKey
            && !c->api_key.key.empty()) {
            return c->api_key.key;
        }
    }

    // settings.json: apiKeys.<provider>
    if (auto h = core::path::home_dir(); h) {
        coding::SettingsManager sm(*h + "/.pi/agent/settings.json");
        const auto& d = sm.get().data;
        if (d.contains("apiKeys") && d["apiKeys"].is_object()) {
            if (auto v = d["apiKeys"].find(provider); v != d["apiKeys"].end() && v->is_string()) {
                return v->get<std::string>();
            }
        }
    }
    return "";
}

std::string get_text_content(const pi::ai::AssistantMessage& m) {
    std::string out;
    for (auto& c : m.content) {
        if (std::holds_alternative<pi::ai::TextContent>(c)) {
            out += std::get<pi::ai::TextContent>(c).text;
        }
    }
    return out;
}

/// Print a session selector (a simple numbered list) and return the chosen index, or -1.
int pick_session(const std::vector<coding::SessionInfo>& sessions) {
    if (sessions.empty()) return -1;
    std::cout << "Available sessions:\n";
    for (size_t i = 0; i < sessions.size() && i < 20; ++i) {
        std::cout << "  [" << (i + 1) << "] " << sessions[i].id
                  << "  " << sessions[i].timestamp
                  << "  " << sessions[i].message_count << " msgs"
                  << (sessions[i].name ? "  " + *sessions[i].name : "")
                  << "\n";
    }
    std::cout << "Enter number (or 0 to cancel): " << std::flush;
    std::string line;
    if (!std::getline(std::cin, line)) return -1;
    try {
        int n = std::stoi(line);
        if (n == 0) return -1;
        if (n >= 1 && n <= (int)sessions.size()) return n - 1;
    } catch (...) {}
    return -1;
}

int run_agent_print_mode(const ai::Model& model, const std::string& prompt,
                         const ai::SimpleStreamOptions& opts, bool as_json,
                         const std::string& resume_path = "",
                         bool no_context_files = false,
                         bool no_skills = false,
                         const std::string& name = "") {
    // Build tools.
    std::string cwd = core::path::current_working_dir().value_or(".");
    std::vector<agent::ToolPtr> tools;
    tools.push_back(std::make_shared<coding::tools::BashTool>());
    tools.push_back(std::make_shared<coding::tools::ReadTool>(cwd));
    tools.push_back(std::make_shared<coding::tools::WriteTool>(cwd));
    tools.push_back(std::make_shared<coding::tools::EditTool>(cwd));
    tools.push_back(std::make_shared<coding::tools::FetchTool>());
    tools.push_back(std::make_shared<coding::tools::WebSearchTool>());

    // Session: resume an existing JSONL session (seed history with its prior
    // messages so the new prompt continues the conversation) or start a fresh
    // one. New entries are appended so the next resume sees the full thread.
    std::unique_ptr<coding::SessionManager> session;
    std::vector<ai::Message> messages;
    std::string session_path = resume_path;
    if (!resume_path.empty()) {
        coding::SessionManager sm(resume_path);
        auto hdr = sm.read_header();
        if (hdr) {
            auto entries = sm.read_entries();
            auto ctx = coding::build_session_context(*hdr, entries, false);
            messages = std::move(ctx.messages);
        } else {
            std::cerr << "error: cannot read session header at " << resume_path << "\n";
            return 2;
        }
        session = std::make_unique<coding::SessionManager>(resume_path);
    } else {
        session_path = coding::SessionManager::default_dir() + "/"
                     + coding::SessionManager::new_session_id() + ".jsonl";
        coding::SessionManager sm(session_path);
        coding::SessionHeader hdr;
        auto slash = sm.path().find_last_of('/');
        hdr.id = sm.path().substr(slash + 1, sm.path().size() - slash - 6 - 1);
        std::time_t t = std::time(nullptr);
        std::tm tm{};
        gmtime_r(&t, &tm);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
        hdr.timestamp = buf;
        hdr.cwd = cwd;
        sm.initialize(hdr);
        session = std::make_unique<coding::SessionManager>(session_path);
        // If a name was provided, persist it as a session_info entry.
        if (!name.empty()) {
            coding::SessionEntry e; e.type = "session_info"; e.data["name"] = name;
            session->append_entry(e);
        }
    }

    // Append the new user prompt to history + session.
    {
        ai::UserMessage um;
        um.content.push_back(ai::TextContent{prompt});
        messages.push_back(um);
        if (session) {
            coding::SessionEntry e;
            e.type = "message";
            e.data["message"] = um.to_json();
            session->append_entry(e);
        }
    }

    // Auto-compaction: summarize the older prefix if the conversation would
    // overflow the model's context window before this turn.
    {
        coding::CompactionSettings cs;
        if (model.context_window > 0) cs.target_context = model.context_window;
        if (coding::should_compact(messages, cs)) {
            auto cr = coding::compact(model, opts, messages, cs);
            if (!cr.aborted && cr.drop_count > 0) {
                messages = cr.kept_messages;
                if (!as_json)
                    std::cerr << "[auto-compacted " << cr.drop_count << " earlier messages]\n";
                if (session) {
                    coding::SessionEntry e;
                    e.type = "compaction";
                    e.data["summary"] = cr.summary;
                    e.data["droppedCount"] = cr.drop_count;
                    session->append_entry(e);
                }
            }
        }
    }

    // Run agent loop.
    agent::AgentLoopConfig cfg;
    cfg.model = model;
    cfg.tools = std::move(tools);
    cfg.stream_opts = opts;
    // System prompt: base coding-agent instructions + project context files
    // (AGENTS.md / CLAUDE.md) + an <available_skills> block, unless suppressed.
    {
        std::vector<coding::ContextFile> ctxf;
        if (!no_context_files) ctxf = coding::load_project_context_files(cwd);
        std::string sp = coding::build_system_prompt(cwd, ctxf);
        if (!no_skills) {
            auto skills = coding::load_skills(cwd);
            sp += coding::format_skills_for_prompt(skills);
        }
        cfg.system_prompt = std::move(sp);
    }

    auto stream = agent::run_agent_loop(std::move(messages), std::move(cfg));

    std::string final_text;
    ai::AssistantMessage last_assistant;
    std::atomic<int> error_count{0};

    auto on_event = [&](const agent::AgentEvent& ev) {
        switch (ev.kind) {
            case agent::AgentEvent::Kind::AgentStart:
                if (as_json) std::cout << "{\"type\":\"agent_start\"}\n";
                break;
            case agent::AgentEvent::Kind::AgentEnd:
                if (as_json) std::cout << "{\"type\":\"agent_end\"}\n";
                break;
            case agent::AgentEvent::Kind::TurnStart:
                if (as_json) std::cout << "{\"type\":\"turn_start\"}\n";
                break;
            case agent::AgentEvent::Kind::TurnEnd:
                if (as_json) std::cout << "{\"type\":\"turn_end\"}\n";
                break;
            case agent::AgentEvent::Kind::MessageStart:
                if (as_json) std::cout << "{\"type\":\"message_start\"}\n";
                break;
            case agent::AgentEvent::Kind::MessageUpdate: {
                auto& aev = ev.assistant_event;
                if (as_json) {
                    core::Json j;
                    j["type"] = "message_update";
                    j["event"] = ai::to_string(aev.kind);
                    if (aev.kind == ai::AssistantMessageEvent::Kind::TextDelta) {
                        j["delta"] = aev.delta;
                    } else if (aev.kind == ai::AssistantMessageEvent::Kind::ToolCallEnd) {
                        j["toolCall"] = {
                            {"id", aev.tool_call.id},
                            {"name", aev.tool_call.name},
                        };
                    }
                    std::cout << j.dump() << "\n";
                } else {
                    if (aev.kind == ai::AssistantMessageEvent::Kind::TextDelta) {
                        std::cout << aev.delta;
                        std::cout.flush();
                    } else if (aev.kind == ai::AssistantMessageEvent::Kind::ToolCallEnd) {
                        std::cerr << "\n[tool: " << aev.tool_call.name << "]\n";
                        std::cerr.flush();
                    }
                }
                break;
            }
            case agent::AgentEvent::Kind::MessageEnd: {
                if (as_json) {
                    std::cout << "{\"type\":\"message_end\"}\n";
                }
                if (std::holds_alternative<ai::AssistantMessage>(ev.message)) {
                    last_assistant = std::get<ai::AssistantMessage>(ev.message);
                }
                // Persist every finalized message (assistant or tool result)
                // so the session can be resumed later, matching interactive.
                if (session) {
                    coding::SessionEntry e;
                    e.type = "message";
                    e.data["message"] = std::visit([](auto& v) -> core::Json {
                        using T = std::decay_t<decltype(v)>;
                        if constexpr (std::is_same_v<T, ai::UserMessage> ||
                                      std::is_same_v<T, ai::AssistantMessage>) {
                            return v.to_json();
                        } else {
                            core::Json j;
                            j["role"] = "toolResult";
                            j["toolCallId"] = v.tool_call_id;
                            j["toolName"] = v.tool_name;
                            j["isError"] = v.is_error;
                            core::Json arr = core::Json::array();
                            for (auto& c : v.content)
                                if (std::holds_alternative<ai::TextContent>(c))
                                    arr.push_back({{"type", "text"},
                                                   {"text", std::get<ai::TextContent>(c).text}});
                            j["content"] = arr;
                            return j;
                        }
                    }, ev.message);
                    session->append_entry(e);
                }
                break;
            }
            case agent::AgentEvent::Kind::ToolExecutionStart: {
                if (as_json) {
                    std::cout << "{\"type\":\"tool_execution_start\",\"tool\":\"" << ev.tool_name
                              << "\",\"args\":" << ev.tool_args.dump() << "}\n";
                } else {
                    std::cerr << "→ " << ev.tool_name << "(" << ev.tool_args.dump() << ")\n";
                    std::cerr.flush();
                }
                break;
            }
            case agent::AgentEvent::Kind::ToolExecutionEnd: {
                if (as_json) {
                    core::Json j;
                    j["type"] = "tool_execution_end";
                    j["tool"] = ev.tool_name;
                    j["isError"] = ev.tool_is_error;
                    j["details"] = ev.tool_result.details;
                    j["content"] = core::Json::array();
                    for (auto& c : ev.tool_result.content) {
                        if (std::holds_alternative<ai::TextContent>(c)) {
                            j["content"].push_back({{"type", "text"},
                                                     {"text", std::get<ai::TextContent>(c).text}});
                        }
                    }
                    std::cout << j.dump() << "\n";
                } else {
                    std::cerr << "← " << ev.tool_name
                              << (ev.tool_is_error ? " [error]\n" : " [ok]\n");
                    std::cerr.flush();
                }
                if (ev.tool_is_error) error_count++;
                break;
            }
            default:
                break;
        }
    };

    stream->drain(on_event);

    final_text = get_text_content(last_assistant);

    if (as_json) {
        core::Json j;
        j["type"] = "done";
        j["stopReason"] = last_assistant.stop_reason;
        j["usage"] = last_assistant.usage;
        std::cout << j.dump() << "\n";
    }

    if (!as_json && last_assistant.stop_reason == "error") {
        std::cerr << "\n[error] "
                  << ai::humanize_stream_error(
                         last_assistant.provider,
                         last_assistant.error_message.value_or(""))
                  << "\n";
        return 3;
    }
    if (g_interrupted) return 130;
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    core::log::init(core::log::Level::Warn);

    // Register all built-in LLM providers (Anthropic, OpenAI, Google, Mistral, MiniMax).
    // Must happen before any stream_simple / find_model call.
    ai::register_builtin_providers();

    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);

    bool show_help = false;
    bool show_version = false;
    bool list_models = false;
    bool list_sessions = false;
    bool as_json = false;
    bool rpc_mode = false;
    bool continue_last = false;     // -c
    bool resume_pick = false;       // -r
    bool no_context_files = false;  // --no-context-files / -nc
    bool no_skills = false;         // --no-skills
    std::string session_name;       // --name
    std::string session_id;         // --session
    std::string export_path;        // --export
    std::string prompt;
    bool has_prompt = false;
    std::string model_id;
    std::string provider_override;
    std::string api_key_override;
    std::optional<int> max_tokens;
    std::optional<double> temperature;
    std::optional<ai::ThinkingLevel> thinking_level;

    for (size_t i = 0; i < args.size(); ++i) {
        auto& a = args[i];
        if (a == "-h" || a == "--help") {
            show_help = true;
        } else if (a == "-V" || a == "--version") {
            show_version = true;
        } else if (a == "--list-models") {
            list_models = true;
        } else if (a == "--list-sessions") {
            list_sessions = true;
        } else if (a == "--mode") {
            if (i + 1 >= args.size()) { std::cerr << "error: --mode requires an argument\n"; return 2; }
            std::string mode = args[++i];
            if (mode == "rpc") rpc_mode = true;
            else if (mode == "json") as_json = true;
            else if (mode != "interactive") {
                std::cerr << "error: unknown mode: " << mode << "\n";
                return 2;
            }
        } else if (a == "--json") {
            as_json = true;
        } else if (a == "-p" || a == "--prompt") {
            if (i + 1 >= args.size()) {
                std::cerr << "error: " << a << " requires an argument\n";
                return 2;
            }
            prompt = args[++i];
            has_prompt = true;
        } else if (a == "--model") {
            if (i + 1 >= args.size()) { std::cerr << "error: --model requires an argument\n"; return 2; }
            model_id = args[++i];
        } else if (a == "--provider") {
            if (i + 1 >= args.size()) { std::cerr << "error: --provider requires an argument\n"; return 2; }
            provider_override = args[++i];
        } else if (a == "--api-key") {
            if (i + 1 >= args.size()) { std::cerr << "error: --api-key requires an argument\n"; return 2; }
            api_key_override = args[++i];
        } else if (a == "--max-tokens") {
            if (i + 1 >= args.size()) { std::cerr << "error: --max-tokens requires an argument\n"; return 2; }
            try { max_tokens = std::stoi(args[++i]); }
            catch (...) { std::cerr << "error: invalid --max-tokens\n"; return 2; }
        } else if (a == "--temperature") {
            if (i + 1 >= args.size()) { std::cerr << "error: --temperature requires an argument\n"; return 2; }
            try { temperature = std::stod(args[++i]); }
            catch (...) { std::cerr << "error: invalid --temperature\n"; return 2; }
        } else if (a == "--thinking") {
            if (i + 1 >= args.size()) { std::cerr << "error: --thinking requires an argument\n"; return 2; }
            std::string tl = args[++i];
            if      (tl == "off")     thinking_level = ai::ThinkingLevel::Off;
            else if (tl == "minimal") thinking_level = ai::ThinkingLevel::Minimal;
            else if (tl == "low")     thinking_level = ai::ThinkingLevel::Low;
            else if (tl == "medium")  thinking_level = ai::ThinkingLevel::Medium;
            else if (tl == "high")    thinking_level = ai::ThinkingLevel::High;
            else if (tl == "xhigh")   thinking_level = ai::ThinkingLevel::XHigh;
            else { std::cerr << "error: unknown thinking level: " << tl << " (off/minimal/low/medium/high/xhigh)\n"; return 2; }
        } else if (a == "-c" || a == "--continue") {
            continue_last = true;
        } else if (a == "-r" || a == "--resume") {
            resume_pick = true;
        } else if (a == "--name") {
            if (i + 1 >= args.size()) { std::cerr << "error: --name requires an argument\n"; return 2; }
            session_name = args[++i];
        } else if (a == "--session") {
            if (i + 1 >= args.size()) { std::cerr << "error: --session requires an argument\n"; return 2; }
            session_id = args[++i];
        } else if (a == "--export") {
            if (i + 1 >= args.size()) { std::cerr << "error: --export requires an argument\n"; return 2; }
            export_path = args[++i];
        } else if (a == "--compact") {
            // No-op for now; /compact slash command in interactive mode handles it.
        } else if (a == "--no-context-files" || a == "-nc") {
            no_context_files = true;
        } else if (a == "--no-skills") {
            no_skills = true;
        } else if (core::str::starts_with(a, "-")) {
            std::cerr << "warn: unknown option: " << a << " (ignored)\n";
        } else {
            if (!has_prompt) {
                prompt = a;
                has_prompt = true;
            } else {
                std::cerr << "warn: extra positional arg: " << a << " (ignored)\n";
            }
        }
    }

    if (show_help) { std::cout << kUsage; return 0; }
    if (show_version) { std::cout << "pi 0.1.0\n"; return 0; }
    if (list_models) { print_models_json(); return 0; }
    if (!export_path.empty()) {
        // Find session to export.
        std::string src;
        if (!session_id.empty()) {
            src = coding::SessionManager::resolve_id_prefix(session_id);
        }
        if (src.empty() && continue_last) {
            auto all = coding::SessionManager::list_all();
            if (!all.empty()) src = all.front().path;
        }
        if (src.empty()) {
            std::cerr << "error: no session to export (use --session <id> or -c)\n";
            return 2;
        }
        int n = coding::export_session_html(src, export_path);
        if (n < 0) {
            std::cerr << "error: failed to export session " << src << " to " << export_path << "\n";
            return 2;
        }
        std::cout << "Exported " << n << " messages to " << export_path << "\n";
        return 0;
    }
    if (rpc_mode) {
        // RPC mode doesn't need a prompt; we'll dispatch after resolving model+key.
    } else if (list_sessions) {
        auto sessions = coding::SessionManager::list_all();
        auto j = core::Json::array();
        for (auto& s : sessions) {
            j.push_back({
                {"id", s.id}, {"path", s.path}, {"timestamp", s.timestamp},
                {"cwd", s.cwd}, {"messageCount", s.message_count},
                {"name", s.name ? *s.name : ""},
            });
        }
        std::cout << j.dump(2) << "\n";
        return 0;
    }
    if (!has_prompt) {
        // No prompt: try interactive mode if stdin is a TTY.
        if (rpc_mode) {
            // Fall through to model + key resolution below.
        } else if (tui::Terminal::is_tty()) {
            // Resolve model first (need API key).
            const ai::Model* model = nullptr;
            if (!model_id.empty()) {
                std::string key = model_id;
                if (key.find('/') == std::string::npos && !provider_override.empty()) {
                    key = provider_override + "/" + key;
                }
                model = ai::find_model(key);
                if (!model) {
                    std::cerr << "error: unknown model: " << key
                              << " (try --list-models)\n";
                    return 2;
                }
            } else {
                if (auto ak = core::env::get("ANTHROPIC_API_KEY"); ak && !ak->empty()) {
                    model = ai::find_model("anthropic/claude-sonnet-4-5");
                } else if (auto ok = core::env::get("OPENAI_API_KEY"); ok && !ok->empty()) {
                    model = ai::find_model("openai/gpt-4o-mini");
                }
                if (!model) {
                    std::cerr << "error: no API key found. Set ANTHROPIC_API_KEY or OPENAI_API_KEY,\n"
                              << "       or use --api-key.\n";
                    return 2;
                }
            }
            ai::SimpleStreamOptions sopts;
            sopts.api_key = !api_key_override.empty() ? api_key_override : resolve_api_key(model->provider);
            if (sopts.api_key->empty()) {
                std::cerr << "error: no API key for provider '" << model->provider << "'.\n";
                return 2;
            }
            sopts.max_tokens = max_tokens;
            sopts.temperature = temperature;
            sopts.reasoning = thinking_level;
            std::string cwd = core::path::current_working_dir().value_or(".");
            // Resolve resume target: --session <id> (prefix), -r (numbered
            // picker), or -c (newest).
            std::string resume_path;
            if (!session_id.empty()) {
                resume_path = coding::SessionManager::resolve_id_prefix(session_id);
                if (resume_path.empty()) {
                    std::cerr << "error: no unique session matches '" << session_id << "'\n";
                    return 2;
                }
            } else if (resume_pick) {
                auto all = coding::SessionManager::list_all();
                int idx = pick_session(all);
                if (idx >= 0) resume_path = all[idx].path;
                // idx < 0 → cancelled: start a fresh session.
            } else if (continue_last) {
                auto all = coding::SessionManager::list_all();
                if (all.empty()) {
                    std::cerr << "error: no saved sessions to resume\n";
                    return 2;
                }
                resume_path = all.front().path;
            }
            std::string sys_prompt;
            {
                std::vector<coding::ContextFile> ctxf;
                if (!no_context_files) ctxf = coding::load_project_context_files(cwd);
                sys_prompt = coding::build_system_prompt(cwd, ctxf);
                if (!no_skills) {
                    auto skills = coding::load_skills(cwd);
                    sys_prompt += coding::format_skills_for_prompt(skills);
                }
            }
            return tui::modes::run_interactive(*model, sopts, cwd, resume_path, sys_prompt,
                                               [&](const std::string& p){ return resolve_api_key(p); });
        }
        if (!rpc_mode) {
            std::cerr << kUsage;
            return 1;
        }
    }

    // Unix citizen: accept piped stdin for print mode.
    // Skip in RPC mode — RPC reads stdin itself in a command loop.
    if (!rpc_mode) {
        if (prompt == "-") {
            std::ostringstream ss;
            ss << std::cin.rdbuf();
            prompt = ss.str();
        } else if (!::isatty(STDIN_FILENO)) {
            std::ostringstream ss;
            ss << std::cin.rdbuf();
            std::string piped = ss.str();
            if (!piped.empty()) {
                if (prompt.empty()) prompt = piped;
                else prompt += "\n\n" + piped;
                has_prompt = true;
            }
        }
    }

    const ai::Model* model = nullptr;
    if (!model_id.empty()) {
        std::string key = model_id;
        if (key.find('/') == std::string::npos && !provider_override.empty()) {
            key = provider_override + "/" + key;
        }
        model = ai::find_model(key);
        if (!model) {
            std::cerr << "error: unknown model: " << key
                      << " (try --list-models)\n";
            return 2;
        }
    } else {
        // Default: pick model based on available keys.
        if (!api_key_override.empty()) {
            // Hard to know which provider; default to anthropic.
            model = ai::find_model("anthropic/claude-sonnet-4-5");
        } else if (auto ak = core::env::get("ANTHROPIC_API_KEY"); ak && !ak->empty()) {
            model = ai::find_model("anthropic/claude-sonnet-4-5");
        } else if (auto ok = core::env::get("OPENAI_API_KEY"); ok && !ok->empty()) {
            model = ai::find_model("openai/gpt-4o-mini");
        } else if (auto mk = core::env::get("MINIMAX_API_KEY"); mk && !mk->empty()) {
            model = ai::find_model("minimax/MiniMax-Text-01");
        }
        if (!model) {
            std::cerr << "error: no API key found. Set ANTHROPIC_API_KEY, OPENAI_API_KEY,"
                      << " MINIMAX_API_KEY, or use --api-key.\n";
            return 2;
        }
    }

    ai::SimpleStreamOptions opts;
    opts.api_key = !api_key_override.empty() ? api_key_override : resolve_api_key(model->provider);
    opts.max_tokens = max_tokens;
    opts.temperature = temperature;
    opts.reasoning = thinking_level;
    if (opts.api_key->empty()) {
        std::cerr << "error: no API key for provider '" << model->provider << "'.\n"
                  << "Set the appropriate environment variable, e.g.\n"
                  << "  export " << (model->provider == "anthropic" ? "ANTHROPIC_API_KEY"
                                      : model->provider == "openai" ? "OPENAI_API_KEY"
                                      : model->provider == "minimax" ? "MINIMAX_API_KEY"
                                      : model->provider == "google" ? "GOOGLE_API_KEY"
                                      : model->provider == "mistral" ? "MISTRAL_API_KEY"
                                                                       : "<PROVIDER>_API_KEY")
                  << "=...\n"
                  << "  or use --api-key.\n";
        return 2;
    }

    if (rpc_mode) {
        return coding::modes::run_rpc_mode(*model, opts,
                                            core::path::current_working_dir().value_or("."),
                                            [&](const std::string& p){ return resolve_api_key(p); });
    }

    // Resolve a session to continue for print mode: --session <id> (prefix),
    // -c (newest), or -r (interactive numbered picker).
    std::string print_resume_path;
    if (!session_id.empty()) {
        print_resume_path = coding::SessionManager::resolve_id_prefix(session_id);
        if (print_resume_path.empty()) {
            std::cerr << "error: no unique session matches '" << session_id << "'\n";
            return 2;
        }
    } else if (resume_pick) {
        auto all = coding::SessionManager::list_all();
        int idx = pick_session(all);
        if (idx < 0) { std::cerr << "no session selected\n"; return 1; }
        print_resume_path = all[idx].path;
    } else if (continue_last) {
        auto all = coding::SessionManager::list_all();
        if (all.empty()) { std::cerr << "error: no saved sessions to continue\n"; return 2; }
        print_resume_path = all.front().path;
    }

    return run_agent_print_mode(*model, prompt, opts, as_json, print_resume_path,
                                no_context_files, no_skills, session_name);
}
