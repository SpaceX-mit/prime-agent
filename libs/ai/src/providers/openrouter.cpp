// libs/ai/src/providers/openrouter.cpp
// OpenRouter: OpenAI-compat proxy, base URL https://openrouter.ai/api/v1
#include "pi_ai/provider.hpp"
#include "pi_ai/providers/openai.hpp"
namespace pi::ai::providers {
class OpenRouterProvider : public Provider {
public:
    ApiKind api() const override { return ApiKind::OpenAICompletions; }
    std::string name() const override { return "openrouter"; }
    std::shared_ptr<EventStream> stream(const Model& model, const Context& ctx,
                                        const StreamOptions& opts) override {
        Model m = model; m.base_url = "https://openrouter.ai/api/v1";
        return OpenAICompletionsProvider().stream(m, ctx, opts);
    }
};
namespace { struct R { R() { ProviderRegistry::instance().register_provider(std::make_shared<OpenRouterProvider>()); } } r; }
}
