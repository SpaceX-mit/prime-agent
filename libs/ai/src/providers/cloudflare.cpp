// libs/ai/src/providers/cloudflare.cpp
// Cloudflare AI Gateway: OpenAI-compat. base_url set per model as the gateway URL.
// Env: CLOUDFLARE_API_KEY (or use --api-key). Model base_url should be:
//   https://gateway.ai.cloudflare.com/v1/<account>/<gateway>/openai
#include "pi_ai/provider.hpp"
#include "pi_ai/providers/openai.hpp"
namespace pi::ai::providers {
class CloudflareProvider : public Provider {
public:
    ApiKind api() const override { return ApiKind::OpenAICompletions; }
    std::string name() const override { return "cloudflare"; }
    std::shared_ptr<EventStream> stream(const Model& model, const Context& ctx,
                                        const StreamOptions& opts) override {
        return OpenAICompletionsProvider().stream(model, ctx, opts);
    }
};
namespace { struct R { R() { ProviderRegistry::instance().register_provider(std::make_shared<CloudflareProvider>()); } } r; }
}
