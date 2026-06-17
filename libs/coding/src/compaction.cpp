// libs/coding/src/compaction.cpp
#include "pi_coding/compaction.hpp"

#include "pi_core/log.hpp"
#include "pi_core/strutil.hpp"

#include <sstream>

namespace pi::coding {

namespace {
std::string message_text(const pi::ai::Message& m) {
    std::ostringstream o;
    std::visit([&](auto& mm) {
        using T = std::decay_t<decltype(mm)>;
        if constexpr (std::is_same_v<T, pi::ai::UserMessage>) {
            o << "[user] ";
            for (auto& c : mm.content) {
                if (std::holds_alternative<pi::ai::TextContent>(c)) {
                    o << std::get<pi::ai::TextContent>(c).text << "\n";
                } else if (std::holds_alternative<pi::ai::ImageContent>(c)) {
                    o << "[image]\n";
                }
            }
        } else if constexpr (std::is_same_v<T, pi::ai::AssistantMessage>) {
            o << "[assistant] ";
            for (auto& c : mm.content) {
                if (std::holds_alternative<pi::ai::TextContent>(c)) {
                    o << std::get<pi::ai::TextContent>(c).text << "\n";
                } else if (std::holds_alternative<pi::ai::ToolCall>(c)) {
                    o << "[tool_call: " << std::get<pi::ai::ToolCall>(c).name << "]\n";
                }
            }
            o << "[stop=" << mm.stop_reason << "]\n";
        } else if constexpr (std::is_same_v<T, pi::ai::ToolResultMessage>) {
            o << "[tool_result: " << mm.tool_name
              << (mm.is_error ? " (error)" : "") << "]\n";
            for (auto& c : mm.content) {
                if (std::holds_alternative<pi::ai::TextContent>(c)) {
                    o << std::get<pi::ai::TextContent>(c).text << "\n";
                }
            }
        }
    }, m);
    return o.str();
}
}  // namespace

int estimate_tokens(const pi::ai::Message& m) {
    std::string t = message_text(m);
    return static_cast<int>((t.size() + 3) / 4);
}

int estimate_tokens(const std::vector<pi::ai::Message>& msgs) {
    int total = 0;
    for (auto& m : msgs) total += estimate_tokens(m);
    return total;
}

bool should_compact(const std::vector<pi::ai::Message>& msgs,
                    const CompactionSettings& s) {
    if (!s.enabled) return false;
    int tokens = estimate_tokens(msgs);
    return tokens > s.target_context - s.reserve_tokens;
}

int find_cut_point(const std::vector<pi::ai::Message>& msgs,
                   const CompactionSettings& s) {
    // Walk from the end, accumulating tokens until we hit keep_recent_tokens.
    int kept = 0;
    int idx = static_cast<int>(msgs.size());
    for (int i = static_cast<int>(msgs.size()) - 1; i >= 0; --i) {
        int t = estimate_tokens(msgs[i]);
        if (kept + t > s.keep_recent_tokens) break;
        kept += t;
        idx = i;
    }
    if (idx <= 1) return -1;  // keep the first message (system context)
    if (idx >= (int)msgs.size()) return -1;
    return idx;
}

std::string serialize_conversation(const std::vector<pi::ai::Message>& msgs) {
    std::ostringstream o;
    for (size_t i = 0; i < msgs.size(); ++i) {
        o << "--- message " << i << " ---\n";
        o << message_text(msgs[i]);
    }
    return o.str();
}

CompactionResult generate_summary(
    const pi::ai::Model& model,
    const pi::ai::SimpleStreamOptions& opts,
    const std::vector<pi::ai::Message>& to_summarize) {
    CompactionResult res;
    if (to_summarize.empty()) {
        res.summary = "(empty conversation)";
        return res;
    }

    // Build the summarization prompt.
    const std::string kSystemPrompt =
        "You are a conversation summarizer. Produce a concise summary of the "
        "conversation provided below. Preserve:\n"
        "- All file paths the assistant read or wrote (with their final state).\n"
        "- All decisions and conclusions made by the assistant.\n"
        "- All unresolved questions or open tasks.\n"
        "- Any code snippets the assistant quoted.\n"
        "Be terse. Use bullet points. Do NOT add commentary or hedging.";

    std::vector<pi::ai::Message> msgs;
    {
        pi::ai::UserMessage um;
        um.content.push_back(pi::ai::TextContent{
            "Summarize this conversation:\n\n" + serialize_conversation(to_summarize)});
        msgs.push_back(um);
    }

    pi::ai::Context ctx;
    ctx.system_prompt = kSystemPrompt;
    ctx.messages = msgs;

    pi::ai::SimpleStreamOptions sopts = opts;
    sopts.temperature = 0.0;  // deterministic
    sopts.max_tokens = 4096;

    auto stream = pi::ai::stream_simple(model, ctx, sopts);
    auto r = stream->result();
    if (!r) {
        res.aborted = true;
        res.summary = "(compaction failed: " + r.error().to_string() + ")";
        return res;
    }
    auto& am = r.value();
    // Extract text from assistant message.
    for (auto& c : am.content) {
        if (std::holds_alternative<pi::ai::TextContent>(c)) {
            if (!res.summary.empty()) res.summary += "\n";
            res.summary += std::get<pi::ai::TextContent>(c).text;
        }
    }
    if (res.summary.empty()) {
        res.summary = "(empty summary)";
    }
    res.drop_count = static_cast<int>(to_summarize.size());
    return res;
}

CompactionResult compact(
    const pi::ai::Model& model,
    const pi::ai::SimpleStreamOptions& opts,
    const std::vector<pi::ai::Message>& msgs,
    const CompactionSettings& settings) {
    CompactionResult res;
    int cut = find_cut_point(msgs, settings);
    if (cut < 0) {
        res.summary = "(nothing to compact)";
        res.kept_messages = msgs;
        return res;
    }

    std::vector<pi::ai::Message> to_summarize(msgs.begin(), msgs.begin() + cut);
    std::vector<pi::ai::Message> kept(msgs.begin() + cut, msgs.end());

    auto summary_res = generate_summary(model, opts, to_summarize);
    if (summary_res.aborted) {
        res.aborted = true;
        res.kept_messages = msgs;
        return res;
    }

    // Build a synthetic summary message that becomes the head of the kept context.
    pi::ai::UserMessage um;
    um.content.push_back(pi::ai::TextContent{
        "[Conversation summary — " + std::to_string(to_summarize.size()) + " earlier messages]\n\n"
        + summary_res.summary});
    res.kept_messages.push_back(um);
    for (auto& m : kept) res.kept_messages.push_back(std::move(m));

    res.summary = summary_res.summary;
    res.drop_count = summary_res.drop_count;
    return res;
}

}  // namespace pi::coding
