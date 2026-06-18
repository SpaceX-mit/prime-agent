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

/// Turn a raw provider/transport error string (e.g. "anthropic: HTTP 429
/// Too Many Requests: {...}") into a short, human-readable sentence the
/// end user can act on. Falls back to a trimmed version of the original
/// when the shape isn't recognized. Used by both interactive and print
/// mode so error reporting is consistent and never dumps raw JSON.
std::string humanize_stream_error(const std::string& provider,
                                  const std::string& raw);

/// Classify a provider/transport error string as retryable. Retryable:
/// HTTP 408/429/500/502/503/504 and transport-level failures (timeouts,
/// connection errors with no HTTP status). NOT retryable: 4xx client
/// errors like 400/401/403/404 (retrying won't help). Used by the agent
/// loop to decide whether to back off and re-issue the request.
bool is_retryable_stream_error(const std::string& raw);

}  // namespace pi::ai
