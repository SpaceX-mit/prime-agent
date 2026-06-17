// libs/ai/include/pi_ai/types.hpp
// LLM Message / Model / Provider types.
//
// Maps to pi's `packages/ai/src/types.ts`.

#pragma once

#include "pi_core/json.hpp"

#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace pi::ai {

// ---------------------------------------------------------------------------
// Content blocks
// ---------------------------------------------------------------------------

struct TextContent {
    std::string text;
    std::string to_json() const { return pi::core::Json{{"type", "text"}, {"text", text}}.dump(); }
};

struct ThinkingContent {
    std::string thinking;
    std::optional<std::string> signature;
    std::string to_json() const {
        pi::core::Json j{{"type", "thinking"}, {"thinking", thinking}};
        if (signature) j["signature"] = *signature;
        return j.dump();
    }
};

struct ImageContent {
    std::string mime_type;
    std::string data;     // base64
    std::string to_json() const {
        return pi::core::Json{{"type", "image"}, {"mimeType", mime_type}, {"data", data}}.dump();
    }
};

struct ToolCall {
    std::string id;
    std::string name;
    /// JSON-encoded arguments.
    std::string arguments_json;
    std::optional<std::vector<std::string>> thoughts;  // anthropic-extended
};

using Content = std::variant<TextContent, ThinkingContent, ImageContent, ToolCall>;

// ---------------------------------------------------------------------------
// Messages
// ---------------------------------------------------------------------------

struct UserMessage {
    std::string role = "user";
    std::vector<Content> content;
    std::optional<std::string> timestamp;

    pi::core::Json to_json() const {
        pi::core::Json j;
        j["role"] = "user";
        j["content"] = serialize_content();
        if (timestamp) j["timestamp"] = *timestamp;
        return j;
    }

private:
    pi::core::Json serialize_content() const;
};

struct AssistantMessage {
    std::string role = "assistant";
    std::vector<Content> content;
    std::string api;
    std::string provider;
    std::string model;
    std::optional<std::string> response_model;
    std::optional<std::string> response_id;
    pi::core::Json usage = pi::core::Json::object();
    std::string stop_reason = "stop";   // "stop" | "length" | "toolUse" | "error" | "aborted"
    std::optional<std::string> error_message;
    int64_t timestamp = 0;

    pi::core::Json to_json() const {
        pi::core::Json j;
        j["role"] = "assistant";
        j["content"] = [&] {
            pi::core::Json arr = pi::core::Json::array();
            for (auto& c : content) {
                std::visit([&](auto& v) {
                    if constexpr (std::is_same_v<std::decay_t<decltype(v)>, TextContent>)
                        arr.push_back({{"type", "text"}, {"text", v.text}});
                    else if constexpr (std::is_same_v<std::decay_t<decltype(v)>, ThinkingContent>) {
                        pi::core::Json t{{"type", "thinking"}, {"thinking", v.thinking}};
                        if (v.signature) t["signature"] = *v.signature;
                        arr.push_back(t);
                    } else if constexpr (std::is_same_v<std::decay_t<decltype(v)>, ImageContent>) {
                        arr.push_back({{"type", "image"}, {"mimeType", v.mime_type}, {"data", v.data}});
                    } else if constexpr (std::is_same_v<std::decay_t<decltype(v)>, ToolCall>) {
                        pi::core::Json t{{"type", "toolCall"}, {"id", v.id}, {"name", v.name}};
                        try { t["arguments"] = pi::core::Json::parse(v.arguments_json); }
                        catch (...) { t["arguments"] = v.arguments_json; }
                        arr.push_back(t);
                    }
                }, c);
            }
            return arr;
        }();
        j["api"] = api;
        j["provider"] = provider;
        j["model"] = model;
        j["stopReason"] = stop_reason;
        j["usage"] = usage;
        if (response_model) j["responseModel"] = *response_model;
        if (response_id) j["responseId"] = *response_id;
        if (error_message) j["errorMessage"] = *error_message;
        j["timestamp"] = timestamp;
        return j;
    }
};

struct ToolResultMessage {
    std::string role = "toolResult";
    std::string tool_call_id;
    std::string tool_name;
    std::vector<Content> content;
    pi::core::Json details = pi::core::Json::object();
    bool is_error = false;
    int64_t timestamp = 0;
};

using Message = std::variant<UserMessage, AssistantMessage, ToolResultMessage>;

// ---------------------------------------------------------------------------
// Models
// ---------------------------------------------------------------------------

enum class ApiKind {
    AnthropicMessages,
    OpenAICompletions,
    OpenAIResponses,
    OpenAICodexWS,
    GoogleGenerativeAI,
    MistralCompletions,
    BedrockConverse,
};

struct Cost {
    double input = 0;
    double output = 0;
    double cache_read = 0;
    double cache_write = 0;
};

struct Model {
    std::string id;            // unique within provider
    std::string name;
    ApiKind api = ApiKind::AnthropicMessages;
    std::string provider;       // e.g. "anthropic"
    std::string base_url;
    bool reasoning = false;     // supports thinking
    std::vector<std::string> input = {"text"};
    Cost cost;
    int context_window = 0;
    int max_tokens = 0;
    std::map<std::string, std::string> headers;
};

// ---------------------------------------------------------------------------
// Tool definitions (input schema for LLM)
// ---------------------------------------------------------------------------

struct ToolSpec {
    std::string name;
    std::string description;
    /// JSON Schema for parameters.
    pi::core::Json parameters = pi::core::Json::object();

    pi::core::Json to_json() const {
        return pi::core::Json{
            {"name", name},
            {"description", description},
            {"parameters", parameters},
        };
    }
};

struct Context {
    std::vector<Message> messages;
    std::vector<ToolSpec> tools;
    std::optional<std::string> system_prompt;
    /// Optional model id override (defaults to Model::id).
    std::optional<std::string> model_id;
};

// ---------------------------------------------------------------------------
// Stream options
// ---------------------------------------------------------------------------

enum class ThinkingLevel { Off, Minimal, Low, Medium, High, XHigh };

struct StreamOptions {
    std::optional<double> temperature;
    std::optional<int> max_tokens;
    std::optional<std::string> api_key;
    int timeout_ms = 60'000;
    int max_retries = 2;
    int max_retry_delay_ms = 60'000;
    std::map<std::string, std::string> extra_headers;
};

struct SimpleStreamOptions : StreamOptions {
    std::optional<ThinkingLevel> reasoning;
};

// ---------------------------------------------------------------------------
// Stream events
// ---------------------------------------------------------------------------

struct AssistantMessageEvent {
    enum class Kind {
        Start,
        TextStart, TextDelta, TextEnd,
        ThinkingStart, ThinkingDelta, ThinkingEnd,
        ToolCallStart, ToolCallDelta, ToolCallEnd,
        Done, Error,
    };
    Kind kind = Kind::Start;
    int content_index = 0;
    std::string delta;          // text/thinking delta
    std::string content;        // final text for *_end
    ToolCall tool_call;         // final tool call for toolcall_end
    std::string reason;         // "stop" | "length" | "toolUse" | "aborted" | "error"
    std::string error_message;
    AssistantMessage partial;    // snapshot

    static AssistantMessageEvent start(AssistantMessage m) {
        AssistantMessageEvent e; e.kind = Kind::Start; e.partial = std::move(m); return e;
    }
    static AssistantMessageEvent text_start(int i, AssistantMessage m) {
        AssistantMessageEvent e; e.kind = Kind::TextStart; e.content_index = i; e.partial = std::move(m); return e;
    }
    static AssistantMessageEvent text_delta(int i, std::string d, AssistantMessage m) {
        AssistantMessageEvent e; e.kind = Kind::TextDelta;
        e.content_index = i; e.delta = std::move(d); e.partial = std::move(m); return e;
    }
    static AssistantMessageEvent text_end(int i, std::string c, AssistantMessage m) {
        AssistantMessageEvent e; e.kind = Kind::TextEnd;
        e.content_index = i; e.content = std::move(c); e.partial = std::move(m); return e;
    }
    static AssistantMessageEvent thinking_delta(int i, std::string d, AssistantMessage m) {
        AssistantMessageEvent e; e.kind = Kind::ThinkingDelta;
        e.content_index = i; e.delta = std::move(d); e.partial = std::move(m); return e;
    }
    static AssistantMessageEvent toolcall_start(int i, AssistantMessage m) {
        AssistantMessageEvent e; e.kind = Kind::ToolCallStart; e.content_index = i; e.partial = std::move(m); return e;
    }
    static AssistantMessageEvent toolcall_delta(int i, std::string d, AssistantMessage m) {
        AssistantMessageEvent e; e.kind = Kind::ToolCallDelta;
        e.content_index = i; e.delta = std::move(d); e.partial = std::move(m); return e;
    }
    static AssistantMessageEvent toolcall_end(int i, ToolCall t, AssistantMessage m) {
        AssistantMessageEvent e; e.kind = Kind::ToolCallEnd;
        e.content_index = i; e.tool_call = std::move(t); e.partial = std::move(m); return e;
    }
    static AssistantMessageEvent done(std::string r, AssistantMessage m) {
        AssistantMessageEvent e; e.kind = Kind::Done; e.reason = std::move(r); e.partial = std::move(m); return e;
    }
    static AssistantMessageEvent error(std::string r, std::string msg, AssistantMessage m) {
        AssistantMessageEvent e; e.kind = Kind::Error;
        e.reason = std::move(r); e.error_message = std::move(msg); e.partial = std::move(m); return e;
    }
};

inline const char* to_string(AssistantMessageEvent::Kind k) {
    switch (k) {
        case AssistantMessageEvent::Kind::Start:         return "start";
        case AssistantMessageEvent::Kind::TextStart:     return "text_start";
        case AssistantMessageEvent::Kind::TextDelta:     return "text_delta";
        case AssistantMessageEvent::Kind::TextEnd:       return "text_end";
        case AssistantMessageEvent::Kind::ThinkingStart: return "thinking_start";
        case AssistantMessageEvent::Kind::ThinkingDelta: return "thinking_delta";
        case AssistantMessageEvent::Kind::ThinkingEnd:   return "thinking_end";
        case AssistantMessageEvent::Kind::ToolCallStart: return "toolcall_start";
        case AssistantMessageEvent::Kind::ToolCallDelta: return "toolcall_delta";
        case AssistantMessageEvent::Kind::ToolCallEnd:   return "toolcall_end";
        case AssistantMessageEvent::Kind::Done:          return "done";
        case AssistantMessageEvent::Kind::Error:         return "error";
    }
    return "?";
}

inline const char* to_string(ApiKind a) {
    switch (a) {
        case ApiKind::AnthropicMessages:    return "anthropic-messages";
        case ApiKind::OpenAICompletions:    return "openai-completions";
        case ApiKind::OpenAIResponses:      return "openai-responses";
        case ApiKind::OpenAICodexWS:        return "openai-codex-websocket";
        case ApiKind::GoogleGenerativeAI:   return "google-generative-ai";
        case ApiKind::MistralCompletions:   return "mistral-completions";
        case ApiKind::BedrockConverse:      return "bedrock-converse";
    }
    return "?";
}

}  // namespace pi::ai
