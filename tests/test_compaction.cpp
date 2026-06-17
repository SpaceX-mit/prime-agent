// tests/test_compaction.cpp
#define TEST_FRAMEWORK_IMPLEMENT
#include "test_framework.hpp"

#include "pi_ai/types.hpp"
#include "pi_coding/compaction.hpp"

#include <string>

using namespace pi;

TEST_CASE("estimate tokens is rough but positive") {
    pi::ai::UserMessage m;
    m.content.push_back(pi::ai::TextContent{std::string(400, 'a')});
    int t = coding::estimate_tokens(m);
    CHECK(t > 0);
    CHECK(t < 200);
}

TEST_CASE("should_compact respects enabled") {
    coding::CompactionSettings s;
    s.enabled = false;
    std::vector<pi::ai::Message> msgs(200);  // a lot
    for (auto& m : msgs) {
        m = pi::ai::UserMessage{};
        std::get<pi::ai::UserMessage>(m).content.push_back(
            pi::ai::TextContent{std::string(1000, 'a')});
    }
    CHECK_FALSE(coding::should_compact(msgs, s));

    s.enabled = true;
    s.target_context = 100;
    CHECK(coding::should_compact(msgs, s));
}

TEST_CASE("find_cut_point keeps recent context") {
    coding::CompactionSettings s;
    s.keep_recent_tokens = 200;

    std::vector<pi::ai::Message> msgs;
    for (int i = 0; i < 20; ++i) {
        pi::ai::UserMessage m;
        m.content.push_back(pi::ai::TextContent{
            std::string(200, static_cast<char>('a' + (i % 26)))});
        msgs.push_back(m);
    }

    int cut = coding::find_cut_point(msgs, s);
    CHECK(cut > 0);
    CHECK(cut < (int)msgs.size());
    // The kept portion must be ≤ keep_recent_tokens + a bit of slack.
    int kept_tokens = 0;
    for (size_t i = cut; i < msgs.size(); ++i) {
        kept_tokens += coding::estimate_tokens(msgs[i]);
    }
    CHECK(kept_tokens <= s.keep_recent_tokens + 100);
}
