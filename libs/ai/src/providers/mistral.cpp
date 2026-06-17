// libs/ai/src/providers/mistral.cpp
// Mistral uses the OpenAI Chat Completions protocol with a different base URL.

#include "pi_ai/providers/mistral.hpp"
#include "pi_ai/providers/openai.hpp"
#include "pi_ai/event_stream.hpp"
#include "pi_ai/types.hpp"

#include <thread>

namespace pi::ai::providers {

std::shared_ptr<EventStream> MistralProvider::stream(
    const Model& model,
    const Context& ctx,
    const StreamOptions& opts) {

    // Rewrite base_url: mistral.ai doesn't use OpenAI's URL.
    Model m = model;
    m.base_url = "https://api.mistral.ai/v1";

    // Delegate to OpenAI Chat Completions parser.
    auto provider = std::make_shared<OpenAICompletionsProvider>();
    return provider->stream(m, ctx, opts);
}

}  // namespace pi::ai::providers
