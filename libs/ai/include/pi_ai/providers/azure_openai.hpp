// libs/ai/include/pi_ai/providers/azure_openai.hpp
#pragma once
#include "../provider.hpp"
namespace pi::ai::providers {
class AzureOpenAIProvider : public Provider {
public:
    ApiKind api() const override { return ApiKind::OpenAICompletions; }
    std::string name() const override { return "azure"; }
    std::shared_ptr<EventStream> stream(const Model& model, const Context& ctx,
                                        const StreamOptions& opts) override;
};
}  // namespace pi::ai::providers
