// libs/ai/include/pi_ai/providers/openai.hpp
#pragma once

#include "../provider.hpp"

namespace pi::ai::providers {

class OpenAICompletionsProvider : public Provider {
public:
    ApiKind api() const override { return ApiKind::OpenAICompletions; }
    std::string name() const override { return "openai"; }

    std::shared_ptr<EventStream> stream(
        const Model& model,
        const Context& ctx,
        const StreamOptions& opts) override;
};

class OpenAIResponsesProvider : public Provider {
public:
    ApiKind api() const override { return ApiKind::OpenAIResponses; }
    std::string name() const override { return "openai"; }

    std::shared_ptr<EventStream> stream(
        const Model& model,
        const Context& ctx,
        const StreamOptions& opts) override;
};

}  // namespace pi::ai::providers
