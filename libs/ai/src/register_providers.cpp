// libs/ai/src/register_providers.cpp
// Self-register built-in providers with the global registry.

#include "pi_ai/provider.hpp"
#include "pi_ai/providers/anthropic.hpp"
#include "pi_ai/providers/google.hpp"
#include "pi_ai/providers/minimax.hpp"
#include "pi_ai/providers/mistral.hpp"
#include "pi_ai/providers/openai.hpp"

namespace pi::ai {

namespace {
struct AutoRegister {
    AutoRegister() {
        ProviderRegistry::instance().register_provider(
            std::make_shared<providers::AnthropicProvider>());
        ProviderRegistry::instance().register_provider(
            std::make_shared<providers::OpenAICompletionsProvider>());
        // OpenAIResponsesProvider is a placeholder (V1); do not register.
        ProviderRegistry::instance().register_provider(
            std::make_shared<providers::GoogleProvider>());
        ProviderRegistry::instance().register_provider(
            std::make_shared<providers::MistralProvider>());
        ProviderRegistry::instance().register_provider(
            std::make_shared<providers::MiniMaxProvider>());
    }
};
}  // namespace

void register_builtin_providers() {
    static AutoRegister kAutoRegister;
    (void)kAutoRegister;  // suppress unused-warning; the ctor does the work.

    // Note: OpenAIResponsesProvider is a stub (not yet implemented) and would
    // shadow OpenAICompletionsProvider if registered with the same name.
    // Only OpenAICompletionsProvider is exposed via name="openai".
}

}  // namespace pi::ai
