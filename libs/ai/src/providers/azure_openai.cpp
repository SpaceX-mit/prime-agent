// libs/ai/src/providers/azure_openai.cpp
// Azure OpenAI: same Chat Completions SSE wire as openai.cpp but with Azure's
// URL scheme and api-key header. Set AZURE_OPENAI_ENDPOINT env var to
// https://<resource>.openai.azure.com and model base_url to the deployment
// name (or leave empty to use model.id as the deployment).
#include "pi_ai/providers/azure_openai.hpp"
#include "pi_ai/providers/openai.hpp"   // reuse build_request_body logic
#include "pi_ai/event_stream.hpp"
#include "pi_core/env.hpp"
#include "pi_core/json.hpp"
#include "pi_http/http_client.hpp"
#include "pi_http/sse_parser.hpp"

#include <thread>

namespace pi::ai::providers {

namespace {

// Build Chat Completions body (shared with OpenAI logic).
pi::core::Json build_azure_body(const Model& model, const Context& ctx,
                                const StreamOptions& opts) {
    pi::core::Json body;
    // Azure ignores the model field in the body (routing is via URL); include
    // it anyway for logging clarity.
    body["model"]  = model.id;
    body["stream"] = true;
    if (opts.max_tokens)  body["max_tokens"]  = *opts.max_tokens;
    if (opts.temperature) body["temperature"] = *opts.temperature;

    if (ctx.system_prompt && !ctx.system_prompt->empty())
        body["messages"] = pi::core::Json::array();  // filled below

    pi::core::Json msgs = pi::core::Json::array();
    if (ctx.system_prompt && !ctx.system_prompt->empty())
        msgs.push_back({{"role","system"}, {"content", *ctx.system_prompt}});

    for (auto& m : ctx.messages) {
        std::visit([&](auto& mm) {
            using T = std::decay_t<decltype(mm)>;
            if constexpr (std::is_same_v<T, UserMessage>) {
                pi::core::Json j{{"role","user"}};
                std::string text;
                for (auto& c : mm.content)
                    if (std::holds_alternative<TextContent>(c))
                        text += std::get<TextContent>(c).text;
                j["content"] = text;
                msgs.push_back(std::move(j));
            } else if constexpr (std::is_same_v<T, AssistantMessage>) {
                pi::core::Json j{{"role","assistant"}};
                std::string text;
                for (auto& c : mm.content)
                    if (std::holds_alternative<TextContent>(c))
                        text += std::get<TextContent>(c).text;
                j["content"] = text;
                msgs.push_back(std::move(j));
            } else {
                msgs.push_back({{"role","tool"}, {"tool_call_id", mm.tool_call_id},
                    {"content", [&]{ std::string s; for (auto& c:mm.content) if (std::holds_alternative<TextContent>(c)) s+=std::get<TextContent>(c).text; return s; }()}});
            }
        }, m);
    }
    body["messages"] = std::move(msgs);

    if (!ctx.tools.empty()) {
        pi::core::Json tools = pi::core::Json::array();
        for (auto& t : ctx.tools)
            tools.push_back({{"type","function"},{"function",{{"name",t.name},{"description",t.description},{"parameters",t.parameters}}}});
        body["tools"] = std::move(tools);
    }
    return body;
}

// Parse a Chat Completions SSE stream the same way openai.cpp does (delta chunks).
void drain_openai_sse(pi::http::SseParser& parser, const std::string& data,
                      AssistantMessage& partial, std::shared_ptr<EventStream> out,
                      int& text_idx, bool& text_started) {
    auto j = pi::core::tryParse(data);
    if (!j) return;
    auto choices = j->find("choices");
    if (choices == j->end() || !choices->is_array() || choices->empty()) return;
    auto& delta = (*choices)[0]["delta"];
    std::string content = delta.value("content", std::string{});
    if (!content.empty()) {
        if (!text_started) {
            out->push(AssistantMessageEvent::text_start(text_idx, partial));
            partial.content.push_back(TextContent{});
            text_started = true;
        }
        std::get<TextContent>(partial.content.back()).text += content;
        out->push(AssistantMessageEvent::text_delta(text_idx, content, partial));
    }
    auto finish = (*choices)[0].value("finish_reason", std::string{});
    if (!finish.empty() && finish != "null") {
        if (text_started)
            out->push(AssistantMessageEvent::text_end(text_idx,
                std::get<TextContent>(partial.content.back()).text, partial));
        partial.stop_reason = finish == "stop" ? "stop" :
                              finish == "length" ? "length" : "stop";
        if (j->contains("usage") && (*j)["usage"].is_object()) {
            auto& u = (*j)["usage"];
            partial.usage["input_tokens"]  = u.value("prompt_tokens",     0);
            partial.usage["output_tokens"] = u.value("completion_tokens", 0);
        }
        out->push(AssistantMessageEvent::done(partial.stop_reason, partial));
    }
    (void)parser;
}

}  // namespace

std::shared_ptr<EventStream> AzureOpenAIProvider::stream(
    const Model& model, const Context& ctx, const StreamOptions& opts) {
    auto out = std::make_shared<EventStream>();
    std::thread([out, model, ctx, opts]() mutable {
        AssistantMessage partial;
        partial.api      = "azure";
        partial.provider = "azure";
        partial.model    = model.id;
        out->push(AssistantMessageEvent::start(partial));

        // URL: base_url is the full Azure endpoint prefix. Fall back to env var.
        std::string endpoint;
        if (!model.base_url.empty()) endpoint = model.base_url;
        else if (auto e = pi::core::env::get("AZURE_OPENAI_ENDPOINT"); e && !e->empty())
            endpoint = *e;
        else {
            partial.stop_reason = "error";
            partial.error_message = "azure: set AZURE_OPENAI_ENDPOINT or model.base_url";
            out->end(std::move(partial)); return;
        }
        // Remove trailing slash.
        if (!endpoint.empty() && endpoint.back() == '/') endpoint.pop_back();
        // Deployment name: model.id or base_url if it looks like just a name.
        std::string deployment = model.id;
        std::string url = endpoint + "/openai/deployments/" + deployment +
                          "/chat/completions?api-version=2024-10-21";

        int text_idx = 0; bool text_started = false;
        pi::http::SseParser parser([&](const pi::http::SseEvent& ev) {
            if (ev.data.empty() || ev.data == "[DONE]") return;
            drain_openai_sse(parser, ev.data, partial, out, text_idx, text_started);
        });

        pi::http::HttpClient client;
        pi::http::HttpRequest req;
        req.method = "POST";
        req.url    = url;
        req.headers["api-key"]       = opts.api_key.value_or("");
        req.headers["Content-Type"]  = "application/json";
        req.body = build_azure_body(model, ctx, opts).dump();
        req.timeout_ms = opts.timeout_ms;

        auto res = client.send_streaming(req, [&](const pi::http::HttpChunk& chunk) -> bool {
            if (!chunk.data.empty()) parser.feed(chunk.data);
            return true;
        });
        if (!res || !res.value().ok()) {
            partial.stop_reason = "error";
            partial.error_message = res ? "azure: HTTP " + std::to_string(res.value().status)
                                        : "azure: " + res.error().to_string();
            out->end(std::move(partial)); return;
        }
        if (partial.stop_reason.empty()) partial.stop_reason = "stop";
        out->end(std::move(partial));
    }).detach();
    return out;
}

// Self-register.
namespace { struct AzureReg { AzureReg() {
    ProviderRegistry::instance().register_provider(std::make_shared<AzureOpenAIProvider>());
} } kAzureReg; }

}  // namespace pi::ai::providers
