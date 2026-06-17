// libs/ai/src/providers/google.cpp
#include "pi_ai/providers/google.hpp"
#include "pi_ai/event_stream.hpp"
#include "pi_core/json.hpp"
#include "pi_http/http_client.hpp"
#include "pi_http/sse_parser.hpp"

#include <chrono>
#include <sstream>
#include <thread>

namespace pi::ai::providers {

namespace {

std::string build_request_body(const Model& model, const Context& ctx,
                               const StreamOptions& opts) {
    pi::core::Json body;
    if (opts.temperature) body["generationConfig"]["temperature"] = *opts.temperature;
    if (opts.max_tokens) body["generationConfig"]["maxOutputTokens"] = *opts.max_tokens;

    // System instruction.
    if (ctx.system_prompt && !ctx.system_prompt->empty()) {
        body["systemInstruction"] = {{"parts", {{{"text", *ctx.system_prompt}}}}};
    }

    pi::core::Json contents = pi::core::Json::array();
    for (auto& m : ctx.messages) {
        std::visit([&](auto& mm) {
            using T = std::decay_t<decltype(mm)>;
            if constexpr (std::is_same_v<T, pi::ai::UserMessage>) {
                pi::core::Json c;
                c["role"] = "user";
                pi::core::Json parts = pi::core::Json::array();
                for (auto& cc : mm.content) {
                    std::visit([&](auto& v) {
                        if constexpr (std::is_same_v<std::decay_t<decltype(v)>, pi::ai::TextContent>) {
                            parts.push_back({{"text", v.text}});
                        } else if constexpr (std::is_same_v<std::decay_t<decltype(v)>, pi::ai::ImageContent>) {
                            parts.push_back({{"inline_data", {{"mime_type", v.mime_type}, {"data", v.data}}}});
                        }
                    }, cc);
                }
                c["parts"] = parts;
                contents.push_back(c);
            } else if constexpr (std::is_same_v<T, pi::ai::AssistantMessage>) {
                pi::core::Json c;
                c["role"] = "model";
                pi::core::Json parts = pi::core::Json::array();
                for (auto& cc : mm.content) {
                    std::visit([&](auto& v) {
                        if constexpr (std::is_same_v<std::decay_t<decltype(v)>, pi::ai::TextContent>) {
                            parts.push_back({{"text", v.text}});
                        } else if constexpr (std::is_same_v<std::decay_t<decltype(v)>, pi::ai::ToolCall>) {
                            pi::core::Json fc;
                            fc["functionCall"] = {
                                {"name", v.name},
                                {"args", pi::core::tryParse(v.arguments_json).value_or(pi::core::Json::object())}};
                            parts.push_back(fc);
                        }
                    }, cc);
                }
                c["parts"] = parts;
                contents.push_back(c);
            } else if constexpr (std::is_same_v<T, pi::ai::ToolResultMessage>) {
                pi::core::Json c;
                c["role"] = "function";
                pi::core::Json parts = pi::core::Json::array();
                pi::core::Json part;
                part["functionResponse"] = {
                    {"name", mm.tool_name},
                    {"response", {{"content", mm.content.empty() ? ""
                                              : std::get<pi::ai::TextContent>(mm.content[0]).text}}}};
                parts.push_back(part);
                c["parts"] = parts;
                contents.push_back(c);
            }
        }, m);
    }
    body["contents"] = contents;

    // Tools (function declarations).
    if (!ctx.tools.empty()) {
        pi::core::Json funcs = pi::core::Json::array();
        for (auto& t : ctx.tools) {
            funcs.push_back({
                {"name", t.name},
                {"description", t.description},
                {"parameters", t.parameters},
            });
        }
        body["tools"] = pi::core::Json{{"functionDeclarations", funcs}};
    }

    return body.dump();
}

std::string map_finish_reason(const std::string& r) {
    if (r == "STOP") return "stop";
    if (r == "MAX_TOKENS") return "length";
    if (r == "SAFETY") return "stop";
    if (r == "RECITATION") return "stop";
    return r;
}

}  // namespace

std::shared_ptr<EventStream> GoogleProvider::stream(
    const Model& model,
    const Context& ctx,
    const StreamOptions& opts) {

    auto out = std::make_shared<EventStream>();
    auto body = build_request_body(model, ctx, opts);
    std::string url = model.base_url
        + "/v1beta/models/" + model.id + ":streamGenerateContent?alt=sse";
    std::string api_key = opts.api_key.value_or("");

    std::thread([out, model, url = std::move(url), body = std::move(body),
                 api_key = std::move(api_key), opts]() mutable {
        AssistantMessage partial;
        partial.api = to_string(model.api);
        partial.provider = model.provider;
        partial.model = model.id;
        partial.timestamp = static_cast<int64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        out->push(AssistantMessageEvent::start(partial));

        int current_text_index = -1;
        std::vector<ToolCall> tool_calls;

        auto sse = std::make_shared<pi::http::SseParser>(
            [&](const pi::http::SseEvent& ev) {
                if (ev.data.empty() || ev.data == "[DONE]") return;
                auto j = pi::core::tryParse(ev.data);
                if (!j) return;
                auto& candidates = (*j)["candidates"];
                if (candidates.is_array() && !candidates.empty()) {
                    auto& cand = candidates[0];
                    if (cand.contains("content") && cand["content"].is_object()) {
                        auto& parts = cand["content"]["parts"];
                        if (parts.is_array()) {
                            for (auto& p : parts) {
                                if (p.contains("text")) {
                                    std::string t = p["text"].get<std::string>();
                                    if (current_text_index == -1) {
                                        current_text_index = (int)partial.content.size();
                                        out->push(AssistantMessageEvent::text_start(current_text_index, partial));
                                        partial.content.push_back(TextContent{""});
                                    }
                                    std::get<TextContent>(partial.content.back()).text += t;
                                    out->push(AssistantMessageEvent::text_delta(current_text_index, t, partial));
                                } else if (p.contains("functionCall")) {
                                    auto& fc = p["functionCall"];
                                    ToolCall tc;
                                    tc.name = fc.value("name", std::string{});
                                    if (fc.contains("args")) tc.arguments_json = fc["args"].dump();
                                    static std::atomic<int> id_counter{0};
                                    tc.id = "call_" + std::to_string(id_counter++);
                                    int idx = (int)tool_calls.size();
                                    tool_calls.push_back(tc);
                                    out->push(AssistantMessageEvent::toolcall_start(idx, partial));
                                    out->push(AssistantMessageEvent::toolcall_end(idx, tc, partial));
                                    partial.content.push_back(tc);
                                }
                            }
                        }
                    }
                    if (cand.contains("finishReason")) {
                        partial.stop_reason = map_finish_reason(
                            cand["finishReason"].get<std::string>());
                    }
                }
                if (j->contains("usageMetadata") && (*j)["usageMetadata"].is_object()) {
                    auto& u = (*j)["usageMetadata"];
                    partial.usage["input_tokens"] = u.value("promptTokenCount", 0);
                    partial.usage["output_tokens"] = u.value("candidatesTokenCount", 0);
                }
            });

        pi::http::HttpRequest req;
        req.method = "POST";
        req.url = url;
        req.body = body;
        req.timeout_ms = opts.timeout_ms;
        req.headers["content-type"] = "application/json";
        req.headers["accept"] = "text/event-stream";
        // Gemini uses ?key= query param.
        req.query["key"] = api_key;
        for (auto& [k, v] : opts.extra_headers) req.headers[k] = v;

        auto client = &pi::http::shared_client();
        std::string body_acc;
        auto resp = client->send_streaming(req, [&](const pi::http::HttpChunk& ch) -> bool {
            if (!ch.data.empty()) {
                body_acc.append(ch.data);
                (void)sse->feed(ch.data);
            }
            return true;
        });
        sse->end_of_stream();

        if (!resp) {
            partial.stop_reason = "error";
            partial.error_message = resp.error().to_string();
            out->end_with_error(resp.error());
            return;
        }
        if (resp.value().status >= 400) {
            partial.stop_reason = "error";
            partial.error_message = "google: HTTP " + std::to_string(resp.value().status)
                                    + ": " + body_acc.substr(0, std::min<size_t>(body_acc.size(), 500));
            out->end(std::move(partial));
            return;
        }

        if (current_text_index >= 0 && current_text_index < (int)partial.content.size()) {
            std::string text = std::get<TextContent>(partial.content[current_text_index]).text;
            out->push(AssistantMessageEvent::text_end(current_text_index, text, partial));
        }
        if (partial.stop_reason.empty()) partial.stop_reason = "stop";
        out->end(std::move(partial));
    }).detach();

    return out;
}

}  // namespace pi::ai::providers
