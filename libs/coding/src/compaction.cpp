// libs/coding/src/compaction.cpp
#include "pi_coding/compaction.hpp"

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
    // Don't compact if there's nothing to drop or result would be too short.
    if (idx <= 1) return -1;  // keep the first message (system context)
    if (idx >= (int)msgs.size()) return -1;
    return idx;
}

}  // namespace pi::coding
