// tests/test_models.cpp
#define TEST_FRAMEWORK_IMPLEMENT
#include "test_framework.hpp"

#include "pi_ai/models.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace pi::ai;

TEST_CASE("builtin models non-empty") {
    auto& ms = builtin_models();
    CHECK(!ms.empty());
    CHECK(ms.size() >= 5);
}

TEST_CASE("find_model with provider prefix") {
    auto* m = find_model("anthropic/claude-sonnet-4-5");
    REQUIRE(m != nullptr);
    CHECK(m->provider == "anthropic");
    CHECK(m->api == ApiKind::AnthropicMessages);
}

TEST_CASE("find_model with bare id") {
    auto* m = find_model("gpt-4o");
    REQUIRE(m != nullptr);
    CHECK(m->provider == "openai");
}

TEST_CASE("find_model unknown returns nullptr") {
    auto* m = find_model("nonexistent/model");
    CHECK(m == nullptr);
}

TEST_CASE("provider_names includes both") {
    auto names = provider_names();
    CHECK(std::find(names.begin(), names.end(), "anthropic") != names.end());
    CHECK(std::find(names.begin(), names.end(), "openai") != names.end());
}
