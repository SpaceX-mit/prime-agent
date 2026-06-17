// libs/ai/include/pi_ai/providers/anthropic.hpp
#pragma once

#include "../provider.hpp"

namespace pi::ai::providers {

class AnthropicProvider : public Provider {
public:
    ApiKind api() const override { return ApiKind::AnthropicMessages; }
    std::string name() const override { return "anthropic"; }

    std::shared_ptr<EventStream> stream(
        const Model& model,
        const Context& ctx,
        const StreamOptions& opts) override;
};

}  // namespace pi::ai::providers
