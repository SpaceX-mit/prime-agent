// tests/test_sse.cpp
#define TEST_FRAMEWORK_IMPLEMENT
#include "test_framework.hpp"

#include "pi_http/sse_parser.hpp"

#include <string>
#include <vector>

using namespace pi::http;

TEST_CASE("SSE simple message") {
    std::vector<SseEvent> events;
    SseParser p([&](const SseEvent& ev) { events.push_back(ev); });
    p.feed("data: hello\n\n");
    REQUIRE(events.size() == 1);
    CHECK(events[0].name == "message");
    CHECK(events[0].data == "hello");
}

TEST_CASE("SSE multi-line data") {
    std::vector<SseEvent> events;
    SseParser p([&](const SseEvent& ev) { events.push_back(ev); });
    p.feed("data: line1\ndata: line2\n\n");
    REQUIRE(events.size() == 1);
    CHECK(events[0].data == "line1\nline2");
}

TEST_CASE("SSE named event") {
    std::vector<SseEvent> events;
    SseParser p([&](const SseEvent& ev) { events.push_back(ev); });
    p.feed("event: ping\ndata: pong\n\n");
    REQUIRE(events.size() == 1);
    CHECK(events[0].name == "ping");
    CHECK(events[0].data == "pong");
}

TEST_CASE("SSE comments ignored") {
    std::vector<SseEvent> events;
    SseParser p([&](const SseEvent& ev) { events.push_back(ev); });
    p.feed(": this is a comment\ndata: ok\n\n");
    REQUIRE(events.size() == 1);
    CHECK(events[0].data == "ok");
}

TEST_CASE("SSE multiple events in one chunk") {
    std::vector<SseEvent> events;
    SseParser p([&](const SseEvent& ev) { events.push_back(ev); });
    p.feed("data: a\n\ndata: b\n\n");
    REQUIRE(events.size() == 2);
    CHECK(events[0].data == "a");
    CHECK(events[1].data == "b");
}

TEST_CASE("SSE end of stream emits partial") {
    std::vector<SseEvent> events;
    SseParser p([&](const SseEvent& ev) { events.push_back(ev); });
    p.feed("data: hello");
    CHECK(events.empty());
    p.end_of_stream();
    REQUIRE(events.size() == 1);
    CHECK(events[0].data == "hello");
}

TEST_CASE("SSE CR stripped") {
    std::vector<SseEvent> events;
    SseParser p([&](const SseEvent& ev) { events.push_back(ev); });
    p.feed("data: hello\r\n\r\n");
    REQUIRE(events.size() == 1);
    CHECK(events[0].data == "hello");
}
