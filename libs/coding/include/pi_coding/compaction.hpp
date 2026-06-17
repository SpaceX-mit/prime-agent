// libs/coding/include/pi_coding/compaction.hpp
// Compaction: summarize old messages when context grows too large.

#pragma once

#include "pi_ai/types.hpp"
#include "pi_core/json.hpp"

#include <string>
#include <vector>

namespace pi::coding {

struct CompactionSettings {
    bool enabled = true;
    int reserve_tokens = 16384;     // keep this many tokens free for new turns
    int keep_recent_tokens = 20000; // and this many tokens of recent context
    int target_context = 200000;    // model default
};

/// Estimate tokens for a message (very rough: ~4 chars per token).
int estimate_tokens(const pi::ai::Message& m);
int estimate_tokens(const std::vector<pi::ai::Message>& msgs);

/// Should we compact now?
bool should_compact(const std::vector<pi::ai::Message>& msgs,
                    const CompactionSettings& s);

/// Find the index where to cut: the boundary after which `keep_recent_tokens`
/// are kept and everything before is summarized.
int find_cut_point(const std::vector<pi::ai::Message>& msgs,
                   const CompactionSettings& s);

/// Result of a compaction.
struct CompactionResult {
    std::string summary;       // LLM-generated summary of the dropped prefix
    std::vector<pi::ai::Message> kept_messages;
    int drop_count = 0;
};

}  // namespace pi::coding
