// libs/ai/src/models.cpp
#include "pi_ai/models.hpp"

namespace pi::ai {

namespace {
const std::vector<Model> kAnthropic = {
    {
        "claude-sonnet-4-5",
        "Claude Sonnet 4.5",
        ApiKind::AnthropicMessages,
        "anthropic",
        "https://api.anthropic.com",
        true,
        {"text", "image"},
        {3.0, 15.0, 0.30, 3.75},
        200'000,
        8192,
        {{"anthropic-version", "2023-06-01"}},
    },
    {
        "claude-opus-4",
        "Claude Opus 4",
        ApiKind::AnthropicMessages,
        "anthropic",
        "https://api.anthropic.com",
        true,
        {"text", "image"},
        {15.0, 75.0, 1.50, 18.75},
        200'000,
        8192,
        {{"anthropic-version", "2023-06-01"}},
    },
    {
        "claude-haiku-4",
        "Claude Haiku 4",
        ApiKind::AnthropicMessages,
        "anthropic",
        "https://api.anthropic.com",
        true,
        {"text", "image"},
        {0.80, 4.0, 0.08, 1.0},
        200'000,
        8192,
        {{"anthropic-version", "2023-06-01"}},
    },
};

const std::vector<Model> kOpenAI = {
    {
        "gpt-4o",
        "GPT-4o",
        ApiKind::OpenAICompletions,
        "openai",
        "https://api.openai.com/v1",
        false,
        {"text", "image"},
        {2.5, 10.0, 1.25, 0},
        128'000,
        16384,
        {},
    },
    {
        "gpt-4o-mini",
        "GPT-4o mini",
        ApiKind::OpenAICompletions,
        "openai",
        "https://api.openai.com/v1",
        false,
        {"text", "image"},
        {0.15, 0.60, 0.075, 0},
        128'000,
        16384,
        {},
    },
    {
        "o1",
        "OpenAI o1",
        ApiKind::OpenAICompletions,
        "openai",
        "https://api.openai.com/v1",
        true,
        {"text"},
        {15.0, 60.0, 7.5, 0},
        200'000,
        100'000,
        {},
    },
    {
        "o3-mini",
        "OpenAI o3-mini",
        ApiKind::OpenAICompletions,
        "openai",
        "https://api.openai.com/v1",
        true,
        {"text"},
        {1.10, 4.40, 0.55, 0},
        200'000,
        100'000,
        {},
    },
};

const std::vector<Model> kAll = [] {
    std::vector<Model> v;
    v.insert(v.end(), kAnthropic.begin(), kAnthropic.end());
    v.insert(v.end(), kOpenAI.begin(), kOpenAI.end());
    return v;
}();
}

const std::vector<Model>& builtin_models() { return kAll; }
const std::vector<Model>& anthropic_models() { return kAnthropic; }
const std::vector<Model>& openai_models() { return kOpenAI; }

const Model* find_model(const std::string& key) {
    // Accept "<provider>/<id>" or just "<id>" (search all).
    std::string_view provider;
    std::string_view id;
    auto slash = key.find('/');
    if (slash != std::string::npos) {
        provider = std::string_view(key).substr(0, slash);
        id = std::string_view(key).substr(slash + 1);
    } else {
        id = key;
    }
    for (auto& m : kAll) {
        if ((provider.empty() || m.provider == provider) && m.id == id) return &m;
    }
    return nullptr;
}

std::vector<std::string> provider_names() {
    std::vector<std::string> out;
    for (auto& m : kAll) {
        if (std::find(out.begin(), out.end(), m.provider) == out.end()) out.push_back(m.provider);
    }
    return out;
}

}  // namespace pi::ai
