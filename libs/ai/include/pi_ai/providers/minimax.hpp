// libs/ai/include/pi_ai/providers/minimax.hpp
//
// MiniMax (稀宇科技 / MiniMax) — Chinese AI company.
//
// API is OpenAI-compatible at https://api.minimaxi.com/v1
// Authentication: Authorization: Bearer <MINIMAX_API_KEY>
//
// Models (publicly known as of 2025):
//   - MiniMax-Text-01   : flagship MoE chat model (~456B params)
//   - MiniMax-VL-01     : vision-language
//   - MiniMax-01        : latest generation
//   - abab-6.5s-chat    : previous generation, fast
//   - abab-6.5-chat     : previous generation, balanced

#pragma once

#include "../provider.hpp"

namespace pi::ai::providers {

/// MiniMax uses the OpenAI Chat Completions protocol with a custom base URL.
class MiniMaxProvider : public Provider {
public:
    ApiKind api() const override { return ApiKind::OpenAICompletions; }
    std::string name() const override { return "minimax"; }

    std::shared_ptr<EventStream> stream(
        const Model& model,
        const Context& ctx,
        const StreamOptions& opts) override;
};

}  // namespace pi::ai::providers
