// tests/test_tools.cpp
// Exercise bash/read/write/edit tools without going through the network.

#define TEST_FRAMEWORK_IMPLEMENT
#include "test_framework.hpp"

#include "pi_agent/tool.hpp"
#include "pi_coding/tools/bash_tool.hpp"
#include "pi_coding/tools/edit_tool.hpp"
#include "pi_coding/tools/read_tool.hpp"
#include "pi_coding/tools/write_tool.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

using namespace pi;

static std::string get_text(const std::vector<pi::ai::Content>& c) {
    for (auto& v : c) {
        if (std::holds_alternative<pi::ai::TextContent>(v)) {
            return std::get<pi::ai::TextContent>(v).text;
        }
    }
    return {};
}

TEST_CASE("bash tool: echo hello") {
    coding::tools::BashTool t;
    pi::core::Json args;
    args["command"] = "echo hello";
    pi::agent::NullAbort sig;
    auto r = t.execute(args, sig, nullptr);
    CHECK_FALSE(r.is_error);
    auto text = get_text(r.content);
    CHECK(text.find("hello") != std::string::npos);
}

TEST_CASE("bash tool: non-zero exit") {
    coding::tools::BashTool t;
    pi::core::Json args;
    args["command"] = "false";
    pi::agent::NullAbort sig;
    auto r = t.execute(args, sig, nullptr);
    CHECK(r.is_error);
}

TEST_CASE("bash tool: invalid command") {
    coding::tools::BashTool t;
    pi::core::Json args;
    args["command"] = 123;  // wrong type
    pi::agent::NullAbort sig;
    auto r = t.execute(args, sig, nullptr);
    CHECK(r.is_error);
}

TEST_CASE("write + read tool") {
    auto tmp = (fs::temp_directory_path() / "pi_test_write.txt").string();
    fs::remove(tmp);

    coding::tools::WriteTool w(fs::temp_directory_path().string());
    pi::core::Json wa;
    wa["path"] = tmp;
    wa["content"] = "line1\nline2\nline3\n";
    pi::agent::NullAbort sig;
    auto wr = w.execute(wa, sig, nullptr);
    CHECK_FALSE(wr.is_error);

    coding::tools::ReadTool r(fs::temp_directory_path().string());
    pi::core::Json ra;
    ra["path"] = tmp;
    auto rr = r.execute(ra, sig, nullptr);
    CHECK_FALSE(rr.is_error);
    auto text = get_text(rr.content);
    CHECK(text.find("line1") != std::string::npos);
    CHECK(text.find("line2") != std::string::npos);
    CHECK(text.find("line3") != std::string::npos);

    fs::remove(tmp);
}

TEST_CASE("edit tool: single replacement") {
    auto tmp = (fs::temp_directory_path() / "pi_test_edit.txt").string();
    fs::remove(tmp);

    coding::tools::WriteTool w(fs::temp_directory_path().string());
    pi::core::Json wa;
    wa["path"] = tmp;
    wa["content"] = "hello world\nhello again\n";
    pi::agent::NullAbort sig;
    auto wr = w.execute(wa, sig, nullptr);
    CHECK_FALSE(wr.is_error);

    coding::tools::EditTool e(fs::temp_directory_path().string());
    pi::core::Json ea;
    ea["path"] = tmp;
    ea["oldString"] = "hello world";
    ea["newString"] = "hi world";
    auto er = e.execute(ea, sig, nullptr);
    CHECK_FALSE(er.is_error);

    coding::tools::ReadTool r(fs::temp_directory_path().string());
    pi::core::Json ra;
    ra["path"] = tmp;
    auto rr = r.execute(ra, sig, nullptr);
    auto text = get_text(rr.content);
    CHECK(text.find("hi world") != std::string::npos);
    CHECK(text.find("hello again") != std::string::npos);

    fs::remove(tmp);
}

TEST_CASE("edit tool: ambiguous oldString fails without allOccurrences") {
    auto tmp = (fs::temp_directory_path() / "pi_test_edit2.txt").string();
    fs::remove(tmp);
    {
        std::ofstream f(tmp); f << "foo bar foo\n";
    }
    coding::tools::EditTool e(fs::temp_directory_path().string());
    pi::core::Json ea;
    ea["path"] = tmp;
    ea["oldString"] = "foo";
    ea["newString"] = "baz";
    pi::agent::NullAbort sig;
    auto er = e.execute(ea, sig, nullptr);
    CHECK(er.is_error);
    fs::remove(tmp);
}

TEST_CASE("edit tool: allOccurrences=true replaces all") {
    auto tmp = (fs::temp_directory_path() / "pi_test_edit3.txt").string();
    fs::remove(tmp);
    {
        std::ofstream f(tmp); f << "foo bar foo\n";
    }
    coding::tools::EditTool e(fs::temp_directory_path().string());
    pi::core::Json ea;
    ea["path"] = tmp;
    ea["oldString"] = "foo";
    ea["newString"] = "baz";
    ea["allOccurrences"] = true;
    pi::agent::NullAbort sig;
    auto er = e.execute(ea, sig, nullptr);
    CHECK_FALSE(er.is_error);

    coding::tools::ReadTool r(fs::temp_directory_path().string());
    pi::core::Json ra;
    ra["path"] = tmp;
    auto rr = r.execute(ra, sig, nullptr);
    auto text = get_text(rr.content);
    CHECK(text.find("baz bar baz") != std::string::npos);
    fs::remove(tmp);
}
