// libs/ai/src/providers/openai_responses.cpp
// OpenAI Responses API provider (/v1/responses). Mirrors the openai.cpp
// Completions provider but targets the newer endpoint used by o4-mini, gpt-4.1.
#include "pi_ai/providers/openai.hpp"
#include "pi_ai/event_stream.hpp"
#include "pi_core/json.hpp"
#include "pi_http/http_client.hpp"
#include "pi_http/sse_parser.hpp"

#include <thread>

namespace pi::ai::providers {

namespace {

pi::core::Json build_responses_body(const Model& model, const Context& ctx,
                                    const StreamOptions& opts) {
    pi::core::Json body;
    body["model"] = model.id;
    body["stream"] = true;
    if (opts.max_tokens)   body["max_output_tokens"] = *opts.max_tokens;
    if (opts.temperature)  body["temperature"] = *opts.temperature;
    if (ctx.system_prompt && !ctx.system_prompt->empty())
        body["instructions"] = *ctx.system_prompt;

    pi::core::Json inputs = pi::core::Json::array();
    for (auto& m : ctx.messages) {
        std::visit([&](auto& mm) {
            using T = std::decay_t<decltype(mm)>;
            if constexpr (std::is_same_v<T, UserMessage>) {
                pi::core::Json inp{{"role","user"}};
                pi::core::Json content = pi::core::Json::array();
                for (auto& c : mm.content)
                    std::visit([&](auto& v) {
                        using C = std::decay_t<decltype(v)>;
                        if constexpr (std::is_same_v<C, TextContent>)
                            content.push_back({{"type","input_text"},{"text",v.text}});
                        else if constexpr (std::is_same_v<C, ImageContent>)
                            content.push_back({{"type","input_image"},
                                {"image_url","data:"+v.mime_type+";base64,"+v.data}});
                    }, c);
                inp["content"] = std::move(content);
                inputs.push_back(std::move(inp));
            } else if constexpr (std::is_same_v<T, AssistantMessage>) {
                pi::core::Json inp{{"role","assistant"}};
                std::string text;
                for (auto& c : mm.content)
                    if (std::holds_alternative<TextContent>(c))
                        text += std::get<TextContent>(c).text;
                inp["content"] = text;
                inputs.push_back(std::move(inp));
            } else {
                pi::core::Json inp;
                inp["type"] = "function_call_output";
                inp["call_id"] = mm.tool_call_id;
                std::string out;
                for (auto& c : mm.content)
                    if (std::holds_alternative<TextContent>(c))
                        out += std::get<TextContent>(c).text;
                inp["output"] = out;
                inputs.push_back(std::move(inp));
            }
        }, m);
    }
    body["input"] = std::move(inputs);

    if (!ctx.tools.empty()) {
        pi::core::Json tools = pi::core::Json::array();
        for (auto& t : ctx.tools)
            tools.push_back({{"type","function"},{"name",t.name},
                {"description",t.description},{"parameters",t.parameters}});
        body["tools"] = std::move(tools);
    }
    return body;
}

}  // namespace

class OpenAIResponsesProvider : public Provider {
public:
    ApiKind api()  const override { return ApiKind::OpenAICompletions; }
    std::string name() const override { return "openai-responses"; }

    std::shared_ptr<EventStream> stream(
        const Model& model, const Context& ctx, const StreamOptions& opts) override
    {
        auto out = std::make_shared<EventStream>();
        std::thread([out, model, ctx, opts]() mutable {
            AssistantMessage partial;
            partial.api      = "openai-responses";
            partial.provider = "openai-responses";
            partial.model    = model.id;
            out->push(AssistantMessageEvent::start(partial));

            int text_idx = 0;
            bool text_started = false;

            pi::http::SseParser parser([&](const pi::http::SseEvent& ev) {
                if (ev.data.empty() || ev.data == "[DONE]") return;
                auto j = pi::core::tryParse(ev.data);
                if (!j) return;
                std::string type = j->value("type", std::string{});
                if (type == "response.output_text.delta") {
                    if (!text_started) {
                        out->push(AssistantMessageEvent::text_start(text_idx, partial));
                        partial.content.push_back(TextContent{});
                        text_started = true;
                    }
                    std::string delta = (*j).value("delta", std::string{});
                    std::get<TextContent>(partial.content.back()).text += delta;
                    out->push(AssistantMessageEvent::text_delta(text_idx, delta, partial));
                } else if (type == "response.completed" || type == "response.done") {
                    if (text_started)
                        out->push(AssistantMessageEvent::text_end(text_idx,
                            text_started ? std::get<TextContent>(partial.content.back()).text : "", partial));
                    partial.stop_reason = "stop";
                    if (j->contains("usage") && (*j)["usage"].is_object()) {
                        auto& u = (*j)["usage"];
                        partial.usage["input_tokens"]  = u.value("input_tokens",  0);
                        partial.usage["output_tokens"] = u.value("output_tokens", 0);
                    }
                    out->push(AssistantMessageEvent::done("stop", partial));
                } else if (type == "error") {
                    partial.stop_reason  = "error";
                    partial.error_message = j->value("message", std::string{"openai-responses error"});
                    out->push(AssistantMessageEvent::error("error", *partial.error_message, partial));
                }
            });

            pi::http::HttpClient client;
            pi::http::HttpRequest req;
            req.method = "POST";
            req.url    = "https://api.openai.com/v1/responses";
            req.headers["Authorization"] = "Bearer " + opts.api_key.value_or("");
            req.headers["Content-Type"]  = "application/json";
            req.body = build_responses_body(model, ctx, opts).dump();
            req.timeout_ms = opts.timeout_ms;

            std::string body_acc;
            auto res = client.send_streaming(req, [&](const pi::http::HttpChunk& chunk) -> bool {
                if (!chunk.data.empty()) parser.feed(chunk.data);
                return true;
            });

            if (!res || !res.value().ok()) {
                partial.stop_reason = "error";
                partial.error_message = res
                    ? "openai-responses: HTTP " + std::to_string(res.value().status)
                    : "openai-responses: " + res.error().to_string();
                out->end(std::move(partial));
                return;
            }
            if (partial.stop_reason.empty()) partial.stop_reason = "stop";
            out->end(std::move(partial));
        }).detach();
        return out;
    }
};

// Self-register at link time.
namespace {
struct RespReg { RespReg() {
    ProviderRegistry::instance().register_provider(
        std::make_shared<OpenAIResponsesProvider>());
} } kRespReg;
}

}  // namespace pi::ai::providers
