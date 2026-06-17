// libs/ai/src/provider.cpp
#include "pi_ai/provider.hpp"

#include <mutex>

namespace pi::ai {

std::shared_ptr<EventStream> Provider::stream_simple(
    const Model& model,
    const Context& ctx,
    const SimpleStreamOptions& opts) {
    StreamOptions base;
    base.temperature = opts.temperature;
    base.max_tokens = opts.max_tokens;
    base.api_key = opts.api_key;
    base.timeout_ms = opts.timeout_ms;
    base.max_retries = opts.max_retries;
    base.max_retry_delay_ms = opts.max_retry_delay_ms;
    base.extra_headers = opts.extra_headers;
    return stream(model, ctx, base);
}

ProviderRegistry& ProviderRegistry::instance() {
    static ProviderRegistry r;
    return r;
}

void ProviderRegistry::register_provider(std::shared_ptr<Provider> p) {
    std::lock_guard<std::mutex> g(mu_);
    by_name_[p->name()] = p;
    api_to_name_[p->api()] = p->name();
}

void ProviderRegistry::unregister_provider(const std::string& name) {
    std::lock_guard<std::mutex> g(mu_);
    by_name_.erase(name);
    for (auto it = api_to_name_.begin(); it != api_to_name_.end(); ) {
        if (it->second == name) it = api_to_name_.erase(it);
        else ++it;
    }
}

std::shared_ptr<Provider> ProviderRegistry::get(const std::string& name) const {
    std::lock_guard<std::mutex> g(mu_);
    auto it = by_name_.find(name);
    return it == by_name_.end() ? nullptr : it->second;
}

std::shared_ptr<Provider> ProviderRegistry::get_for_api(ApiKind api) const {
    std::lock_guard<std::mutex> g(mu_);
    auto it = api_to_name_.find(api);
    if (it == api_to_name_.end()) return nullptr;
    return get(it->second);
}

std::vector<std::string> ProviderRegistry::provider_names() const {
    std::lock_guard<std::mutex> g(mu_);
    std::vector<std::string> out;
    out.reserve(by_name_.size());
    for (auto& [k, _] : by_name_) out.push_back(k);
    return out;
}

}  // namespace pi::ai
