// libs/ai/include/pi_ai/providers/google.hpp
#pragma once

#include "../provider.hpp"

namespace pi::ai::providers {

class GoogleProvider : public Provider {
public:
    ApiKind api() const override { return ApiKind::GoogleGenerativeAI; }
    std::string name() const override { return "google"; }

    std::shared_ptr<EventStream> stream(
        const Model& model,
        const Context& ctx,
        const StreamOptions& opts) override;
};

}  // namespace pi::ai::providers
