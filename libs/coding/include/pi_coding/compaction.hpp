// libs/coding/include/pi_coding/compaction.hpp
// Compaction: summarize old messages when context grows too large.

#pragma once

#include "pi_ai/stream_simple.hpp"
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

/// Serialize a conversation to plain text (for summarization prompts).
std::string serialize_conversation(const std::vector<pi::ai::Message>& msgs);

/// Result of a compaction.
struct CompactionResult {
    std::string summary;       // LLM-generated summary of the dropped prefix
    std::vector<pi::ai::Message> kept_messages;
    int drop_count = 0;
    bool aborted = false;
};

/// Generate a summary by calling the LLM. Blocks until completion.
/// Returns CompactionResult.summary on success.
CompactionResult generate_summary(
    const pi::ai::Model& model,
    const pi::ai::SimpleStreamOptions& opts,
    const std::vector<pi::ai::Message>& to_summarize);

/// Convenience: full compaction pipeline (find cut point + summarize).
/// Returns the result plus the kept messages. The summary is meant to be
/// prepended to the kept messages as a "compaction" entry.
CompactionResult compact(
    const pi::ai::Model& model,
    const pi::ai::SimpleStreamOptions& opts,
    const std::vector<pi::ai::Message>& msgs,
    const CompactionSettings& settings);

}  // namespace pi::coding

