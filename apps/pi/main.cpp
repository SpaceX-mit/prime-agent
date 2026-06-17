// apps/pi/main.cpp
// Phase 0-1 entry point. Supports:
//   --help / -h         show usage
//   --version / -V      show version
//   --list-models       list built-in models
//   -p <prompt>         one-shot stream to stdout (uses model from env or --model)
//   --model <id>        specify model (e.g. "anthropic/claude-sonnet-4-5")
//   --api-key <key>     override API key (default: env var)
//   --json              emit JSON events instead of plain text (print mode)

#include "pi_ai/models.hpp"
#include "pi_ai/stream_simple.hpp"
#include "pi_ai/types.hpp"
#include "pi_core/ansi.hpp"
#include "pi_core/env.hpp"
#include "pi_core/json.hpp"
#include "pi_core/log.hpp"
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
  pi [options]                Interactive mode (not implemented yet)
  pi -p "<prompt>"            One-shot prompt (print mode, streaming to stdout)
  echo "..." | pi -p "-"      Read prompt from stdin

OPTIONS:
  -h, --help                  Show this help
  -V, --version               Print version
      --list-models           List all built-in models (json)
      --model <id>            Model id (e.g. anthropic/claude-sonnet-4-5)
      --provider <name>       Provider name (e.g. anthropic)
      --api-key <key>         Override API key (default: env var)
      --max-tokens <n>        Limit output tokens
      --temperature <f>       Sampling temperature
  -p <text>                   Print mode (one-shot, streaming)
      --json                  Print mode emits JSON events instead of plain text

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

int run_print_mode(const ai::Model& model, const std::string& prompt,
                   const ai::SimpleStreamOptions& opts, bool as_json) {
    ai::Context ctx;
    ctx.messages.push_back(ai::UserMessage{});
    std::get<ai::UserMessage>(ctx.messages.back()).content.push_back(ai::TextContent{prompt});

    ai::AssistantMessage final_msg;
    auto stream = ai::stream_simple(model, ctx, opts);

    bool first_text = true;
    auto on_event = [&](const ai::AssistantMessageEvent& ev) {
        if (as_json) {
            core::Json j;
            j["type"] = ai::to_string(ev.kind);
            j["model"] = model.id;
            if (ev.kind == ai::AssistantMessageEvent::Kind::TextDelta) {
                j["delta"] = ev.delta;
            } else if (ev.kind == ai::AssistantMessageEvent::Kind::ToolCallEnd) {
                j["toolCall"] = {
                    {"id", ev.tool_call.id},
                    {"name", ev.tool_call.name},
                    {"arguments", ev.tool_call.arguments_json},
                };
            } else if (ev.kind == ai::AssistantMessageEvent::Kind::Done) {
                j["stopReason"] = ev.reason;
                j["usage"] = ev.partial.usage;
            } else if (ev.kind == ai::AssistantMessageEvent::Kind::Error) {
                j["error"] = ev.error_message;
            }
            std::cout << j.dump() << "\n";
            std::cout.flush();
        } else {
            if (ev.kind == ai::AssistantMessageEvent::Kind::TextDelta) {
                if (first_text) first_text = false;
                std::cout << ev.delta;
                std::cout.flush();
            } else if (ev.kind == ai::AssistantMessageEvent::Kind::Done) {
                if (first_text) {}  // no text emitted
                std::cout << "\n";
            } else if (ev.kind == ai::AssistantMessageEvent::Kind::Error) {
                std::cerr << "\n[error] " << ev.error_message << "\n";
                std::cerr.flush();
            }
        }
    };

    auto res = stream->drain_to_completion(on_event);
    if (!res) {
        std::cerr << "[fatal] " << res.error().to_string() << "\n";
        return 2;
    }
    final_msg = std::move(res.value());

    if (as_json) {
        core::Json j;
        j["type"] = "done";
        j["stopReason"] = final_msg.stop_reason;
        j["usage"] = final_msg.usage;
        std::cout << j.dump() << "\n";
    }

    if (final_msg.stop_reason == "error") {
        std::cerr << "[error] " << final_msg.error_message.value_or("(no message)") << "\n";
        return 3;
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    core::log::init(core::log::Level::Warn);

    // First pass: collect options.
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
    if (show_version) { std::cout << "pi " << "0.1.0" << "\n"; return 0; }
    if (list_models) { print_models_json(); return 0; }
    if (!has_prompt) {
        std::cerr << kUsage;
        return 1;
    }

    // Read from stdin if prompt == "-"
    if (prompt == "-") {
        std::ostringstream ss;
        ss << std::cin.rdbuf();
        prompt = ss.str();
    }

    // Resolve model.
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
        // Default: pick the first available based on env.
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
    if (!provider_override.empty() && model->provider != provider_override) {
        std::cerr << "warn: --provider overrides model default (" << model->provider
                  << " -> " << provider_override << ") but model stays as '"
                  << model->id << "'\n";
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

    return run_print_mode(*model, prompt, opts, as_json);
}
