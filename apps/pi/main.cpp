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
#include "pi_ai/stream_simple.hpp"
#include "pi_ai/types.hpp"
#include "pi_coding/tools/bash_tool.hpp"
#include "pi_coding/tools/edit_tool.hpp"
#include "pi_coding/tools/read_tool.hpp"
#include "pi_coding/tools/write_tool.hpp"
#include "pi_core/ansi.hpp"
#include "pi_core/env.hpp"
#include "pi_core/json.hpp"
#include "pi_core/log.hpp"
#include "pi_core/path.hpp"
#include "pi_core/strutil.hpp"

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

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
  -p <text>                   Print mode (agent loop with bash/read/write/edit)
      --json                  Emit JSON events instead of plain text

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
    if (auto k = core::env::get("PRIME_AGENT_API_KEY"); k && !k->empty()) return *k;
    if (provider == "anthropic") {
        if (auto k = core::env::get("ANTHROPIC_API_KEY"); k && !k->empty()) return *k;
    } else if (provider == "openai") {
        if (auto k = core::env::get("OPENAI_API_KEY"); k && !k->empty()) return *k;
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

int run_agent_print_mode(const ai::Model& model, const std::string& prompt,
                         const ai::SimpleStreamOptions& opts, bool as_json) {
    // Build tools.
    std::string cwd = core::path::current_working_dir().value_or(".");
    std::vector<agent::ToolPtr> tools;
    tools.push_back(std::make_shared<coding::tools::BashTool>());
    tools.push_back(std::make_shared<coding::tools::ReadTool>(cwd));
    tools.push_back(std::make_shared<coding::tools::WriteTool>(cwd));
    tools.push_back(std::make_shared<coding::tools::EditTool>(cwd));

    // Build initial messages.
    std::vector<ai::Message> messages;
    {
        ai::UserMessage um;
        um.content.push_back(ai::TextContent{prompt});
        messages.push_back(um);
    }

    // Run agent loop.
    agent::AgentLoopConfig cfg;
    cfg.model = model;
    cfg.tools = std::move(tools);
    cfg.stream_opts = opts;

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
        std::cerr << "\n[error] " << last_assistant.error_message.value_or("(no message)") << "\n";
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

    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);

    bool show_help = false;
    bool show_version = false;
    bool list_models = false;
    bool as_json = false;
    std::string prompt;
    bool has_prompt = false;
    std::string model_id;
    std::string provider_override;
    std::string api_key_override;
    std::optional<int> max_tokens;
    std::optional<double> temperature;

    for (size_t i = 0; i < args.size(); ++i) {
        auto& a = args[i];
        if (a == "-h" || a == "--help") {
            show_help = true;
        } else if (a == "-V" || a == "--version") {
            show_version = true;
        } else if (a == "--list-models") {
            list_models = true;
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
    if (!has_prompt) {
        std::cerr << kUsage;
        return 1;
    }

    if (prompt == "-") {
        std::ostringstream ss;
        ss << std::cin.rdbuf();
        prompt = ss.str();
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
        if (auto ak = core::env::get("ANTHROPIC_API_KEY"); ak && !ak->empty()) {
            model = ai::find_model("anthropic/claude-sonnet-4-5");
        } else if (auto ok = core::env::get("OPENAI_API_KEY"); ok && !ok->empty()) {
            model = ai::find_model("openai/gpt-4o-mini");
        }
        if (!model) {
            std::cerr << "error: no API key found. Set ANTHROPIC_API_KEY or OPENAI_API_KEY,"
                      << " or use --api-key.\n";
            return 2;
        }
    }

    ai::SimpleStreamOptions opts;
    opts.api_key = !api_key_override.empty() ? api_key_override : resolve_api_key(model->provider);
    opts.max_tokens = max_tokens;
    opts.temperature = temperature;
    if (opts.api_key->empty()) {
        std::cerr << "error: no API key for provider '" << model->provider << "'.\n"
                  << "Set the appropriate environment variable, e.g.\n"
                  << "  export " << (model->provider == "anthropic" ? "ANTHROPIC_API_KEY" : "OPENAI_API_KEY")
                  << "=...\n"
                  << "  or use --api-key.\n";
        return 2;
    }

    return run_agent_print_mode(*model, prompt, opts, as_json);
}
