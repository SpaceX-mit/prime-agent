// libs/ai/src/providers/minimax.cpp
// MiniMax (MiniMax) reuses OpenAI Chat Completions parser, just rewrites base URL.

#include "pi_ai/providers/minimax.hpp"
#include "pi_ai/providers/openai.hpp"
#include "pi_ai/event_stream.hpp"
#include "pi_ai/types.hpp"

#include <string>

namespace pi::ai::providers {

std::shared_ptr<EventStream> MiniMaxProvider::stream(
    const Model& model,
    const Context& ctx,
    const StreamOptions& opts) {

    // MiniMax uses the OpenAI protocol at api.minimaxi.com (mainland China).
    // The base_url is already set on the model entry; the OpenAI provider
    // now uses model.base_url, so we delegate directly.
    auto provider = std::make_shared<OpenAICompletionsProvider>();
    return provider->stream(model, ctx, opts);
}

}  // namespace pi::ai::providers
