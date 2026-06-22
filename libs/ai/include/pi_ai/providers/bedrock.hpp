// libs/ai/include/pi_ai/providers/bedrock.hpp
#pragma once
#include "../provider.hpp"
namespace pi::ai::providers {
class BedrockConverseProvider : public Provider {
public:
    ApiKind api() const override { return ApiKind::OpenAICompletions; }
    std::string name() const override { return "bedrock"; }
    std::shared_ptr<EventStream> stream(const Model& model, const Context& ctx,
                                        const StreamOptions& opts) override;
};
}  // namespace pi::ai::providers
