// tests/test_session.cpp
#define TEST_FRAMEWORK_IMPLEMENT
#include "test_framework.hpp"

#include "pi_coding/auth_storage.hpp"
#include "pi_coding/html_export.hpp"
#include "pi_coding/session_manager.hpp"
#include "pi_coding/settings_manager.hpp"
#include "pi_core/file_io.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

using namespace pi;

TEST_CASE("session resume: build_session_context replays user/assistant/tool messages") {
    auto dir = fs::temp_directory_path() / "pi_test_session_resume";
    fs::create_directories(dir);
    auto path = (dir / "resume.jsonl").string();
    fs::remove(path);

    coding::SessionManager sm(path);
    coding::SessionHeader h;
    h.id = "resume1";
    h.timestamp = "2026-01-01T00:00:00Z";
    h.cwd = "/tmp";
    REQUIRE(sm.initialize(h).is_ok());

    // user → assistant (text + toolCall) → toolResult → user (turn 2)
    auto append = [&](const core::Json& msg) {
        coding::SessionEntry e;
        e.type = "message";
        e.data["message"] = msg;
        CHECK(sm.append_entry(e).is_ok());
    };
    append({{"role", "user"},
            {"content", core::Json::array({core::Json{{"type", "text"}, {"text", "ping"}}})}});
    append({{"role", "assistant"},
            {"content", core::Json::array({
                core::Json{{"type", "text"}, {"text", "pong"}},
                core::Json{{"type", "toolCall"}, {"id", "tc1"}, {"name", "bash"},
                           {"arguments", core::Json{{"command", "echo hi"}}}},
            })},
            {"stopReason", "tool_use"}});
    append({{"role", "toolResult"}, {"toolCallId", "tc1"}, {"toolName", "bash"},
            {"isError", false},
            {"content", core::Json::array({core::Json{{"type", "text"}, {"text", "hi"}}})}});
    append({{"role", "user"},
            {"content", core::Json::array({core::Json{{"type", "text"}, {"text", "again"}}})}});

    auto hdr = sm.read_header();
    REQUIRE(hdr.has_value());
    auto entries = sm.read_entries();
    auto ctx = coding::build_session_context(*hdr, entries, false);
    REQUIRE(ctx.messages.size() == 4);

    REQUIRE(std::holds_alternative<ai::UserMessage>(ctx.messages[0]));
    REQUIRE(std::holds_alternative<ai::AssistantMessage>(ctx.messages[1]));
    REQUIRE(std::holds_alternative<ai::ToolResultMessage>(ctx.messages[2]));
    REQUIRE(std::holds_alternative<ai::UserMessage>(ctx.messages[3]));

    auto& am = std::get<ai::AssistantMessage>(ctx.messages[1]);
    CHECK(am.stop_reason == "tool_use");
    bool saw_text = false, saw_call = false;
    for (auto& c : am.content) {
        if (std::holds_alternative<ai::TextContent>(c) &&
            std::get<ai::TextContent>(c).text == "pong") saw_text = true;
        if (std::holds_alternative<ai::ToolCall>(c) &&
            std::get<ai::ToolCall>(c).name == "bash") saw_call = true;
    }
    CHECK(saw_text);
    CHECK(saw_call);

    auto& tr = std::get<ai::ToolResultMessage>(ctx.messages[2]);
    CHECK(tr.tool_name == "bash");
    CHECK_FALSE(tr.is_error);

    fs::remove_all(dir);
}

TEST_CASE("session entry encode/decode") {
    coding::SessionEntry e;
    e.type = "message";
    e.data["message"] = {{"role", "user"}, {"content", "hello"}};
    auto line = e.encode();
    auto decoded = coding::SessionEntry::decode(line);
    REQUIRE(decoded.has_value());
    CHECK(decoded->type == "message");
    CHECK(decoded->data["message"]["role"] == "user");
}

TEST_CASE("session new id is non-empty") {
    auto id = coding::SessionManager::new_session_id();
    CHECK(id.size() >= 16);
    // IDs from the same instant may collide rarely; just check format.
    CHECK(id.find(' ') == std::string::npos);
}

TEST_CASE("session initialize + read_header") {
    auto dir = fs::temp_directory_path() / "pi_test_session";
    fs::create_directories(dir);
    auto path = (dir / "test.jsonl").string();
    fs::remove(path);

    coding::SessionManager sm(path);
    coding::SessionHeader h;
    h.id = "test123";
    h.timestamp = "2026-01-01T00:00:00Z";
    h.cwd = "/tmp";
    auto r = sm.initialize(h);
    CHECK(r.is_ok());

    auto h2 = sm.read_header();
    REQUIRE(h2.has_value());
    CHECK(h2->id == "test123");
    CHECK(h2->version == coding::kCurrentSessionVersion);

    fs::remove_all(dir);
}

TEST_CASE("session append + read entries") {
    auto dir = fs::temp_directory_path() / "pi_test_session2";
    fs::create_directories(dir);
    auto path = (dir / "test2.jsonl").string();
    fs::remove(path);

    coding::SessionManager sm(path);
    coding::SessionHeader h;
    h.id = "abc";
    h.timestamp = "2026-01-01T00:00:00Z";
    h.cwd = "/tmp";
    REQUIRE(sm.initialize(h).is_ok());

    coding::SessionEntry e1;
    e1.type = "message";
    e1.data["message"] = {{"role", "user"}, {"content", "hi"}};
    CHECK(sm.append_entry(e1).is_ok());

    coding::SessionEntry e2;
    e2.type = "message";
    e2.data["message"] = {{"role", "assistant"}, {"content", "hello"}};
    CHECK(sm.append_entry(e2).is_ok());

    auto entries = sm.read_entries();
    CHECK(entries.size() >= 3);  // header + 2 messages
    fs::remove_all(dir);
}

TEST_CASE("settings manager merge") {
    auto dir = fs::temp_directory_path() / "pi_test_settings";
    fs::create_directories(dir);
    auto global = (dir / "global.json").string();
    auto project = (dir / "project.json").string();
    {
        std::ofstream f(global);
        f << R"({"defaultModel": "anthropic/claude-sonnet-4-5", "x": 1})" << "\n";
    }
    {
        std::ofstream f(project);
        f << R"({"x": 2, "y": 3})" << "\n";
    }
    coding::SettingsManager sm(global, project);
    auto& s = sm.get();
    CHECK(s.data["defaultModel"] == "anthropic/claude-sonnet-4-5");
    CHECK(s.data["x"] == 2);  // project overrides
    CHECK(s.data["y"] == 3);

    fs::remove_all(dir);
}

TEST_CASE("auth storage roundtrip") {
    auto dir = fs::temp_directory_path() / "pi_test_auth";
    fs::create_directories(dir);
    auto path = (dir / "auth.json").string();
    fs::remove(path);

    coding::AuthStorage s(path);
    coding::AuthCredential c;
    c.type = coding::AuthCredential::Type::ApiKey;
    c.api_key.key = "sk-test-1234";
    s.set("anthropic", c);

    auto got = s.get("anthropic");
    REQUIRE(got.has_value());
    CHECK(got->type == coding::AuthCredential::Type::ApiKey);
    CHECK(got->api_key.key == "sk-test-1234");

    auto names = s.list();
    CHECK(std::find(names.begin(), names.end(), "anthropic") != names.end());

    s.remove("anthropic");
    CHECK_FALSE(s.get("anthropic").has_value());

    fs::remove_all(dir);
}

TEST_CASE("session resolve_id_prefix") {
    // Create two sessions with known prefixes.
    auto dir = fs::temp_directory_path() / "pi_test_session3";
    fs::create_directories(dir);
    fs::remove_all(dir);
    fs::create_directories(dir);

    coding::SessionManager sm1((dir / "AAAA1111AAAA.jsonl").string());
    coding::SessionHeader h;
    h.id = "AAAA1111AAAA"; h.timestamp = "2026-01-01T00:00:00Z"; h.cwd = "/tmp";
    REQUIRE(sm1.initialize(h).is_ok());

    coding::SessionManager sm2((dir / "BBBB2222BBBB.jsonl").string());
    h.id = "BBBB2222BBBB"; h.timestamp = "2026-01-02T00:00:00Z";
    REQUIRE(sm2.initialize(h).is_ok());

    auto resolved = coding::SessionManager::resolve_id_prefix("AAAA", dir.string());
    CHECK(resolved.find("AAAA") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("html export roundtrip") {
    auto dir = fs::temp_directory_path() / "pi_test_html_export";
    fs::remove_all(dir);
    fs::create_directories(dir);
    auto path = (dir / "test.jsonl").string();
    coding::SessionManager sm(path);
    coding::SessionHeader h;
    h.id = "html01"; h.timestamp = "2026-01-01T00:00:00Z"; h.cwd = "/tmp";
    REQUIRE(sm.initialize(h).is_ok());

    coding::SessionEntry e1;
    e1.type = "message";
    e1.data["message"] = {{"role", "user"}, {"content", "hello"}};
    REQUIRE(sm.append_entry(e1).is_ok());

    coding::SessionEntry e2;
    e2.type = "message";
    e2.data["message"] = {{"role", "assistant"}, {"content", "hi there"}};
    REQUIRE(sm.append_entry(e2).is_ok());

    auto html_path = (dir / "out.html").string();
    int n = coding::export_session_html(path, html_path);
    CHECK(n == 2);
    CHECK(fs::exists(html_path));

    // Sanity: HTML contains the words.
    auto content_r = core::file::read(html_path);
    REQUIRE(content_r.is_ok());
    auto content = content_r.value();
    CHECK(content.find("<!DOCTYPE html>") != std::string::npos);
    CHECK(content.find("hello") != std::string::npos);
    CHECK(content.find("hi there") != std::string::npos);
    CHECK(content.find("message-user") != std::string::npos);
    CHECK(content.find("message-assistant") != std::string::npos);

    fs::remove_all(dir);
}
