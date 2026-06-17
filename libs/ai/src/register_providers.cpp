// libs/ai/src/register_providers.cpp
// Self-register built-in providers with the global registry.

#include "pi_ai/provider.hpp"
#include "pi_ai/providers/anthropic.hpp"
#include "pi_ai/providers/google.hpp"
#include "pi_ai/providers/openai.hpp"

namespace pi::ai {

namespace {
struct AutoRegister {
    AutoRegister() {
        ProviderRegistry::instance().register_provider(
            std::make_shared<providers::AnthropicProvider>());
        ProviderRegistry::instance().register_provider(
            std::make_shared<providers::OpenAICompletionsProvider>());
        ProviderRegistry::instance().register_provider(
            std::make_shared<providers::OpenAIResponsesProvider>());
        ProviderRegistry::instance().register_provider(
            std::make_shared<providers::GoogleProvider>());
    }
};
static AutoRegister kAutoRegister;
}

}  // namespace pi::ai
