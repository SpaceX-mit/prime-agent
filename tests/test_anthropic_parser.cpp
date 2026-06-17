// tests/test_anthropic_parser.cpp
#define TEST_FRAMEWORK_IMPLEMENT
#include "test_framework.hpp"

#include "pi_http/sse_parser.hpp"

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

using namespace pi::http;

TEST_CASE("SSE Anthropic message_start") {
    std::vector<std::string> lines = {
        "data: {\"type\":\"message_start\",\"message\":{\"id\":\"msg_1\",\"usage\":{\"input_tokens\":10,\"output_tokens\":0}}}\n\n",
        "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"text\",\"text\":\"\"}}\n\n",
        "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\"hello \"}}\n\n",
        "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\"world\"}}\n\n",
        "data: {\"type\":\"content_block_stop\",\"index\":0}\n\n",
        "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\"},\"usage\":{\"output_tokens\":2}}\n\n",
        "data: {\"type\":\"message_stop\"}\n\n",
    };
    std::vector<SseEvent> sse;
    SseParser p([&](const SseEvent& ev) { sse.push_back(ev); });
    for (auto& l : lines) {
        p.feed(l);
    }
    p.end_of_stream();
    REQUIRE(sse.size() == 7);

    auto j0 = nlohmann::ordered_json::parse(sse[0].data);
    CHECK(j0["type"] == "message_start");
    CHECK(j0["message"]["usage"]["input_tokens"] == 10);

    auto j2 = nlohmann::ordered_json::parse(sse[2].data);
    CHECK(j2["delta"]["text"] == "hello ");
}

TEST_CASE("SSE Anthropic tool_use") {
    std::vector<std::string> lines = {
        "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"tool_use\",\"id\":\"toolu_1\",\"name\":\"read\"}}\n\n",
        "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"{\\\"path\\\":\"}}\n\n",
        "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"\\\"/tmp/x\\\"}\"}}\n\n",
        "data: {\"type\":\"content_block_stop\",\"index\":0}\n\n",
    };
    std::vector<SseEvent> sse;
    SseParser p([&](const SseEvent& ev) { sse.push_back(ev); });
    for (auto& l : lines) (void)p.feed(l);
    p.end_of_stream();
    REQUIRE(sse.size() == 4);
    auto j2 = nlohmann::ordered_json::parse(sse[1].data);
    auto partial = j2["delta"]["partial_json"].get<std::string>();
    CHECK(partial == std::string("{\"path\":"));
}
