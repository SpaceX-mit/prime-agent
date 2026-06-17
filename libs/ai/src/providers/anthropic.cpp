// libs/ai/src/providers/anthropic.cpp
#include "pi_ai/providers/anthropic.hpp"
#include "pi_ai/event_stream.hpp"
#include "pi_core/json.hpp"
#include "pi_core/log.hpp"
#include "pi_http/http_client.hpp"
#include "pi_http/sse_parser.hpp"

#include <chrono>
#include <sstream>
#include <thread>

namespace pi::ai::providers {

namespace {

// Build Anthropic Messages request body.
std::string build_request_body(const Model& model, const Context& ctx,
                               const StreamOptions& opts) {
    pi::core::Json body;
    body["model"] = model.id;
    body["max_tokens"] = opts.max_tokens.value_or(8192);
    if (opts.temperature) body["temperature"] = *opts.temperature;
    body["stream"] = true;

    // System prompt.
    if (ctx.system_prompt && !ctx.system_prompt->empty()) {
        body["system"] = *ctx.system_prompt;
    }

    // Messages
    pi::core::Json msgs = pi::core::Json::array();
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
                            content.push_back({{"type", "image"},
                                               {"source", {{"type", "base64"},
                                                           {"media_type", v.mime_type},
                                                           {"data", v.data}}}});
                        }
                    }, c);
                }
                j["content"] = std::move(content);
                msgs.push_back(std::move(j));
            } else if constexpr (std::is_same_v<T, AssistantMessage>) {
                pi::core::Json j{{"role", "assistant"}};
                pi::core::Json content = pi::core::Json::array();
                for (auto& c : mm.content) {
                    std::visit([&](auto& v) {
                        if constexpr (std::is_same_v<std::decay_t<decltype(v)>, TextContent>) {
                            content.push_back({{"type", "text"}, {"text", v.text}});
                        } else if constexpr (std::is_same_v<std::decay_t<decltype(v)>, ToolCall>) {
                            pi::core::Json t{{"type", "tool_use"},
                                              {"id", v.id},
                                              {"name", v.name}};
                            try { t["input"] = pi::core::Json::parse(v.arguments_json); }
                            catch (...) { t["input"] = v.arguments_json; }
                            content.push_back(std::move(t));
                        }
                    }, c);
                }
                j["content"] = std::move(content);
                msgs.push_back(std::move(j));
            } else if constexpr (std::is_same_v<T, ToolResultMessage>) {
                pi::core::Json j{{"role", "user"}};
                pi::core::Json content = pi::core::Json::array();
                pi::core::Json block{{"type", "tool_result"},
                                      {"tool_use_id", mm.tool_call_id}};
                pi::core::Json arr = pi::core::Json::array();
                for (auto& c : mm.content) {
                    std::visit([&](auto& v) {
                        if constexpr (std::is_same_v<std::decay_t<decltype(v)>, TextContent>) {
                            arr.push_back({{"type", "text"}, {"text", v.text}});
                        }
                    }, c);
                }
                if (arr.empty()) arr.push_back({{"type", "text"}, {"text", ""}});
                block["content"] = std::move(arr);
                if (mm.is_error) block["is_error"] = true;
                content.push_back(std::move(block));
                j["content"] = std::move(content);
                msgs.push_back(std::move(j));
            }
        }, m);
    }
    body["messages"] = std::move(msgs);

    // Tools
    if (!ctx.tools.empty()) {
        pi::core::Json tools = pi::core::Json::array();
        for (auto& t : ctx.tools) {
            tools.push_back({
                {"name", t.name},
                {"description", t.description},
                {"input_schema", t.parameters},
            });
        }
        body["tools"] = std::move(tools);
    }

    return body.dump();
}

std::string map_stop_reason(const std::string& r) {
    if (r == "end_turn") return "stop";
    if (r == "max_tokens") return "length";
    if (r == "tool_use") return "toolUse";
    if (r == "stop_sequence") return "stop";
    return r;
}

}  // namespace

std::shared_ptr<EventStream> AnthropicProvider::stream(
    const Model& model,
    const Context& ctx,
    const StreamOptions& opts) {

    auto out = std::make_shared<EventStream>();
    auto body = build_request_body(model, ctx, opts);
    std::string url = "https://api.anthropic.com/v1/messages";
    std::string api_key = opts.api_key.value_or("");

    // Spawn a worker thread that performs the HTTP request.
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

        // Track per-block state by index.
        struct Block {
            enum Type { Unknown, Text, Thinking, ToolUse } type = Unknown;
            std::string text_buf;
            std::string name;
            std::string id;
            std::string json_buf;
        };
        std::vector<Block> blocks;

        auto finish_block = [&](int i) {
            if (i < 0 || i >= (int)blocks.size()) return;
            auto& b = blocks[i];
            if (b.type == Block::Text) {
                partial.content.push_back(TextContent{b.text_buf});
                out->push(AssistantMessageEvent::text_end(i, b.text_buf, partial));
            } else if (b.type == Block::ToolUse) {
                ToolCall tc;
                tc.id = b.id;
                tc.name = b.name;
                tc.arguments_json = b.json_buf;
                partial.content.push_back(tc);
                out->push(AssistantMessageEvent::toolcall_end(i, tc, partial));
            }
        };

        bool stream_done = false;
        auto sse = std::make_shared<pi::http::SseParser>(
            [&](const pi::http::SseEvent& ev) {
                auto j = pi::core::tryParse(ev.data);
                if (!j) return;
                std::string type = j->value("type", std::string{});
                if (type == "message_start") {
                    if (auto& msg = (*j)["message"]; msg.is_object()) {
                        partial.response_id = msg.value("id", std::string{});
                        if (auto& u = msg["usage"]; u.is_object()) {
                            partial.usage["input_tokens"] = u.value("input_tokens", 0);
                            partial.usage["output_tokens"] = u.value("output_tokens", 0);
                        }
                    }
                } else if (type == "content_block_start") {
                    int idx = j->value("index", 0);
                    if ((int)blocks.size() <= idx) blocks.resize(idx + 1);
                    auto& blk = blocks[idx];
                    if (auto& cb = (*j)["content_block"]; cb.is_object()) {
                        std::string bt = cb.value("type", std::string{});
                        if (bt == "text") {
                            blk.type = Block::Text;
                            out->push(AssistantMessageEvent::text_start(idx, partial));
                        } else if (bt == "thinking") {
                            blk.type = Block::Thinking;
                            out->push(AssistantMessageEvent::thinking_delta(idx, cb.value("thinking", std::string{}), partial));
                        } else if (bt == "tool_use") {
                            blk.type = Block::ToolUse;
                            blk.id = cb.value("id", std::string{});
                            blk.name = cb.value("name", std::string{});
                            out->push(AssistantMessageEvent::toolcall_start(idx, partial));
                        }
                    }
                } else if (type == "content_block_delta") {
                    int idx = j->value("index", 0);
                    if ((int)blocks.size() <= idx) return;
                    auto& blk = blocks[idx];
                    if (auto& d = (*j)["delta"]; d.is_object()) {
                        std::string dt = d.value("type", std::string{});
                        if (dt == "text_delta") {
                            std::string t = d.value("text", std::string{});
                            blk.text_buf += t;
                            out->push(AssistantMessageEvent::text_delta(idx, t, partial));
                        } else if (dt == "thinking_delta") {
                            std::string t = d.value("thinking", std::string{});
                            out->push(AssistantMessageEvent::thinking_delta(idx, t, partial));
                        } else if (dt == "input_json_delta") {
                            std::string p = d.value("partial_json", std::string{});
                            blk.json_buf += p;
                            out->push(AssistantMessageEvent::toolcall_delta(idx, p, partial));
                        }
                    }
                } else if (type == "content_block_stop") {
                    int idx = j->value("index", 0);
                    finish_block(idx);
                } else if (type == "message_delta") {
                    if (auto& d = (*j)["delta"]; d.is_object()) {
                        partial.stop_reason = map_stop_reason(d.value("stop_reason", std::string{}));
                    }
                    if (auto& u = (*j)["usage"]; u.is_object()) {
                        partial.usage["output_tokens"] = u.value("output_tokens", 0);
                    }
                } else if (type == "message_stop") {
                    stream_done = true;
                } else if (type == "error") {
                    std::string em = j->value("error", pi::core::Json::object())
                                          .value("message", std::string{});
                    partial.stop_reason = "error";
                    partial.error_message = em;
                    out->push(AssistantMessageEvent::error("error", em, partial));
                }
            });

        pi::http::HttpRequest req;
        req.method = "POST";
        req.url = url;
        req.body = body;
        req.timeout_ms = opts.timeout_ms;
        req.headers["content-type"] = "application/json";
        req.headers["x-api-key"] = api_key;
        req.headers["anthropic-version"] = "2023-06-01";
        req.headers["accept"] = "text/event-stream";
        for (auto& [k, v] : opts.extra_headers) req.headers[k] = v;

        // Apply model headers too.
        for (auto& [k, v] : model.headers) {
            if (!k.empty()) req.headers[k] = v;
        }

        auto client = &pi::http::shared_client();
        std::string body_acc;
        auto resp = client->send_streaming(req, [&](const pi::http::HttpChunk& ch) -> bool {
            if (!ch.data.empty()) {
                body_acc.append(ch.data);
                auto r = sse->feed(ch.data);
                if (!r) {
                    return false;
                }
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
            std::string err = "anthropic: HTTP " + std::to_string(resp.value().status)
                              + " " + resp.value().status_text
                              + ": " + body_acc.substr(0, std::min<size_t>(body_acc.size(), 500));
            partial.stop_reason = "error";
            partial.error_message = err;
            out->end(std::move(partial));
            return;
        }

        if (partial.stop_reason.empty() || partial.stop_reason == "stop") {
            partial.stop_reason = stream_done ? "stop" : "error";
        }
        if (partial.stop_reason == "error" && !partial.error_message) {
            partial.error_message = "anthropic: stream ended unexpectedly";
        }
        out->end(std::move(partial));
    }).detach();

    return out;
}

}  // namespace pi::ai::providers
