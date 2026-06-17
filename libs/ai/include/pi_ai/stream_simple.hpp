// libs/ai/include/pi_ai/stream_simple.hpp
#pragma once

#include "event_stream.hpp"
#include "models.hpp"
#include "types.hpp"

#include <memory>
#include <string>

namespace pi::ai {

/// High-level helper that:
/// 1. Looks up the model by id (e.g. "anthropic/claude-sonnet-4-5").
/// 2. Looks up the provider.
/// 3. Streams.
/// Errors NEVER throw; the returned EventStream always ends with a final
/// AssistantMessage whose `stop_reason` is "error" or "aborted".
std::shared_ptr<EventStream> stream_simple(
    const Model& model,
    const Context& ctx,
    const SimpleStreamOptions& opts);

/// Pull all events and return the final message.
/// Equivalent to JS `await consume(streamSimple(...))`.
AssistantMessage run_to_completion(
    const Model& model,
    const Context& ctx,
    const SimpleStreamOptions& opts);

}  // namespace pi::ai
