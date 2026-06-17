// libs/ai/include/pi_ai/providers/mistral.hpp
#pragma once

#include "../provider.hpp"

namespace pi::ai::providers {

/// Mistral reuses OpenAI Chat Completions protocol.
class MistralProvider : public Provider {
public:
    ApiKind api() const override { return ApiKind::MistralCompletions; }
    std::string name() const override { return "mistral"; }

    std::shared_ptr<EventStream> stream(
        const Model& model,
        const Context& ctx,
        const StreamOptions& opts) override;
};

}  // namespace pi::ai::providers
