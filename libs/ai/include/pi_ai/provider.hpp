// libs/ai/include/pi_ai/provider.hpp
// Provider interface and built-in providers.

#pragma once

#include "event_stream.hpp"
#include "types.hpp"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace pi::ai {

/// Base provider — translates a `Context` into an HTTP request to the upstream
/// API, parses the streaming response, and emits `AssistantMessageEvent`s.
class Provider {
public:
    virtual ~Provider() = default;

    /// The model protocol this provider implements.
    virtual ApiKind api() const = 0;
    /// The provider name (e.g. "anthropic").
    virtual std::string name() const = 0;

    /// Stream a completion. Returns an EventStream the caller can drain.
    virtual std::shared_ptr<EventStream> stream(
        const Model& model,
        const Context& ctx,
        const StreamOptions& opts) = 0;

    /// Convenience: `stream` with `SimpleStreamOptions`.
    std::shared_ptr<EventStream> stream_simple(
        const Model& model,
        const Context& ctx,
        const SimpleStreamOptions& opts);
};

// ---------------------------------------------------------------------------
// Provider registry
// ---------------------------------------------------------------------------

class ProviderRegistry {
public:
    static ProviderRegistry& instance();

    void register_provider(std::shared_ptr<Provider> p);
    void unregister_provider(const std::string& name);

    std::shared_ptr<Provider> get(const std::string& name) const;
    std::shared_ptr<Provider> get_for_api(ApiKind api) const;

    std::vector<std::string> provider_names() const;

private:
    ProviderRegistry() = default;
    mutable std::mutex mu_;
    std::unordered_map<std::string, std::shared_ptr<Provider>> by_name_;
    std::unordered_map<ApiKind, std::string> api_to_name_;
};

/// Register all built-in providers (Anthropic, OpenAI, Google, Mistral, MiniMax).
/// Call this once at startup to defeat linker dead-code elimination of the
/// static AutoRegister instance.
void register_builtin_providers();

}  // namespace pi::ai
