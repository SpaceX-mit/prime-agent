// libs/ai/src/providers/openai.cpp
#include "pi_ai/providers/openai.hpp"
#include "pi_ai/event_stream.hpp"
#include "pi_core/json.hpp"
#include "pi_http/http_client.hpp"
#include "pi_http/sse_parser.hpp"

#include <chrono>
#include <thread>

namespace pi::ai::providers {

namespace {

std::string build_request_body(const Model& model, const Context& ctx,
                               const StreamOptions& opts) {
    pi::core::Json body;
    body["model"] = model.id;
    body["stream"] = true;
    if (opts.temperature) body["temperature"] = *opts.temperature;
    if (opts.max_tokens) body["max_tokens"] = *opts.max_tokens;

    pi::core::Json msgs = pi::core::Json::array();
    if (ctx.system_prompt && !ctx.system_prompt->empty()) {
        msgs.push_back({{"role", "system"}, {"content", *ctx.system_prompt}});
    }
    for (auto& m : ctx.messages) {
        std::visit([&](auto& mm) {
            using T = std::decay_t<decltype(mm)>;
            if constexpr (std::is_same_v<T, UserMessage>) {
                pi::core::Json j{{"role", "user"}};
                pi::core::Json content = pi::core::Json::array();
                for (auto& c : mm.content) {
                    std::visit([&](auto& v) {
                        if constexpr (std::is_same_v<std::decay_t<decltype(v)>, TextContent>) {
                            content.push_back({{"type", "text"}, {"text", v.text}});
                        } else if constexpr (std::is_same_v<std::decay_t<decltype(v)>, ImageContent>) {
                            content.push_back({{"type", "image_url"},
                                               {"image_url", {{"url", "data:" + v.mime_type + ";base64," + v.data}}}});
                        }
                    }, c);
                }
                if (content.size() == 1 && (*content.begin())["type"] == "text") {
                    j["content"] = (*content.begin())["text"];
                } else {
                    j["content"] = std::move(content);
                }
                msgs.push_back(std::move(j));
            } else if constexpr (std::is_same_v<T, AssistantMessage>) {
                pi::core::Json j{{"role", "assistant"}};
                std::string text;
                pi::core::Json tool_calls = pi::core::Json::array();
                for (auto& c : mm.content) {
                    std::visit([&](auto& v) {
                        if constexpr (std::is_same_v<std::decay_t<decltype(v)>, TextContent>) {
                            text += v.text;
                        } else if constexpr (std::is_same_v<std::decay_t<decltype(v)>, ToolCall>) {
                            pi::core::Json tc;
                            tc["id"] = v.id;
                            tc["type"] = "function";
                            tc["function"] = {{"name", v.name},
                                              {"arguments", v.arguments_json}};
                            tool_calls.push_back(std::move(tc));
                        }
                    }, c);
                }
                if (!text.empty()) j["content"] = text;
                if (!tool_calls.empty()) j["tool_calls"] = std::move(tool_calls);
                msgs.push_back(std::move(j));
            } else if constexpr (std::is_same_v<T, ToolResultMessage>) {
                std::string content;
                for (auto& c : mm.content) {
                    std::visit([&](auto& v) {
                        if constexpr (std::is_same_v<std::decay_t<decltype(v)>, TextContent>) {
                            if (!content.empty()) content += '\n';
                            content += v.text;
                        }
                    }, c);
                }
                msgs.push_back({
                    {"role", "tool"},
                    {"tool_call_id", mm.tool_call_id},
                    {"content", content},
                });
            }
        }, m);
    }
    body["messages"] = std::move(msgs);

    if (!ctx.tools.empty()) {
        pi::core::Json tools = pi::core::Json::array();
        for (auto& t : ctx.tools) {
            tools.push_back({
                {"type", "function"},
                {"function", {{"name", t.name},
                              {"description", t.description},
                              {"parameters", t.parameters}}},
            });
        }
        body["tools"] = std::move(tools);
    }

    return body.dump();
}

std::string map_stop_reason(const std::string& r) {
    if (r == "stop") return "stop";
    if (r == "length") return "length";
    if (r == "tool_calls") return "toolUse";
    if (r == "content_filter") return "stop";
    return r;
}

}  // namespace

std::shared_ptr<EventStream> OpenAICompletionsProvider::stream(
    const Model& model,
    const Context& ctx,
    const StreamOptions& opts) {

    auto out = std::make_shared<EventStream>();
    auto body = build_request_body(model, ctx, opts);
    std::string url = "https://api.openai.com/v1/chat/completions";
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

        // Track tool calls by index (chat completions streams partial arguments).
        struct ToolCallPartial {
            std::string id;
            std::string name;
            std::string arguments;
        };
        std::vector<ToolCallPartial> tool_calls;
        int current_text_index = -1;

        auto sse = std::make_shared<pi::http::SseParser>(
            [&](const pi::http::SseEvent& ev) {
                if (ev.data.empty() || ev.data == "[DONE]") return;
                auto j = pi::core::tryParse(ev.data);
                if (!j || !j->contains("choices")) return;
                for (auto& ch : (*j)["choices"]) {
                    auto& delta = ch["delta"];
                    if (delta.contains("content") && !delta["content"].is_null()) {
                        std::string t = delta["content"].get<std::string>();
                        if (current_text_index == -1) {
                            current_text_index = (int)partial.content.size();
                            out->push(AssistantMessageEvent::text_start(current_text_index, partial));
                            partial.content.push_back(TextContent{""});
                        }
                        std::get<TextContent>(partial.content.back()).text += t;
                        out->push(AssistantMessageEvent::text_delta(current_text_index, t, partial));
                    }
                    if (delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
                        for (auto& tc : delta["tool_calls"]) {
                            int idx = tc.value("index", 0);
                            if ((int)tool_calls.size() <= idx) tool_calls.resize(idx + 1);
                            auto& tp = tool_calls[idx];
                            if (tc.contains("id")) tp.id = tc["id"].get<std::string>();
                            if (tc.contains("function") && tc["function"].is_object()) {
                                auto& fn = tc["function"];
                                if (fn.contains("name")) tp.name = fn["name"].get<std::string>();
                                if (fn.contains("arguments")) tp.arguments += fn["arguments"].get<std::string>();
                            }
                        }
                    }
                    if (ch.contains("finish_reason") && !ch["finish_reason"].is_null()) {
                        partial.stop_reason = map_stop_reason(ch["finish_reason"].get<std::string>());
                    }
                }
                if (j->contains("usage") && (*j)["usage"].is_object()) {
                    partial.usage = (*j)["usage"];
                }
            });

        pi::http::HttpRequest req;
        req.method = "POST";
        req.url = url;
        req.body = body;
        req.timeout_ms = opts.timeout_ms;
        req.headers["content-type"] = "application/json";
        req.headers["authorization"] = "Bearer " + api_key;
        req.headers["accept"] = "text/event-stream";
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
            partial.error_message = "openai: HTTP " + std::to_string(resp.value().status)
                                    + ": " + body_acc.substr(0, std::min<size_t>(body_acc.size(), 500));
            out->end(std::move(partial));
            return;
        }

        // Finalize text block if any.
        if (current_text_index >= 0 && current_text_index < (int)partial.content.size()) {
            std::string text = std::get<TextContent>(partial.content[current_text_index]).text;
            out->push(AssistantMessageEvent::text_end(current_text_index, text, partial));
        }
        // Finalize tool calls.
        for (size_t i = 0; i < tool_calls.size(); ++i) {
            ToolCall tc;
            tc.id = tool_calls[i].id;
            tc.name = tool_calls[i].name;
            tc.arguments_json = tool_calls[i].arguments;
            partial.content.push_back(tc);
            out->push(AssistantMessageEvent::toolcall_end((int)i, tc, partial));
        }

        if (partial.stop_reason.empty()) partial.stop_reason = "stop";
        out->end(std::move(partial));
    }).detach();

    return out;
}

std::shared_ptr<EventStream> OpenAIResponsesProvider::stream(
    const Model& model,
    const Context& ctx,
    const StreamOptions& /*opts*/) {
    // V1: not implemented — OpenAI Responses API is a newer protocol that
    // differs from Chat Completions. Use the chat completions provider for now.
    auto out = std::make_shared<EventStream>();
    std::thread([out, model]() {
        AssistantMessage m;
        m.api = to_string(model.api);
        m.provider = model.provider;
        m.model = model.id;
        m.stop_reason = "error";
        m.error_message = "openai-responses: not yet implemented in V1";
        out->end(std::move(m));
    }).detach();
    return out;
}

}  // namespace pi::ai::providers
