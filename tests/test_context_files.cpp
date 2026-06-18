// tests/test_context_files.cpp
// P2.2: project context-file discovery (AGENTS.md / CLAUDE.md) and
// system-prompt injection. Mirrors upstream resource-loader behavior:
// global agent dir first, then ancestors root→cwd, dedup by path.
#define TEST_FRAMEWORK_IMPLEMENT
#include "test_framework.hpp"

#include "pi_coding/context_files.hpp"
#include "pi_core/file_io.hpp"

#include <cstdlib>
#include <string>
#include <unistd.h>

using namespace pi::coding;

static std::string mk_tmp_tree() {
    // Unique-ish dir without Date/random: use pid.
    std::string base = "/tmp/pi_ctxtest_" + std::to_string(::getpid());
    pi::core::file::create_directories(base + "/a/b");
    pi::core::file::write_atomic(base + "/a/AGENTS.md", "ROOT-RULES");
    pi::core::file::write_atomic(base + "/a/b/CLAUDE.md", "LEAF-RULES");
    return base;
}

static bool contains(const std::string& h, const std::string& n) {
    return h.find(n) != std::string::npos;
}

TEST_CASE("context files: discover ancestors root->cwd, dedup") {
    std::string base = mk_tmp_tree();
    auto files = load_project_context_files(base + "/a/b", base + "/no_agent_dir");
    REQUIRE(files.size() == 2);
    // Ancestor order: /a/AGENTS.md before /a/b/CLAUDE.md.
    CHECK(contains(files[0].path, "/a/AGENTS.md"));
    CHECK(contains(files[1].path, "/a/b/CLAUDE.md"));
    CHECK(files[0].content == "ROOT-RULES");
    CHECK(files[1].content == "LEAF-RULES");
}

TEST_CASE("system prompt injects project_context block in order") {
    std::string base = mk_tmp_tree();
    auto files = load_project_context_files(base + "/a/b", base + "/no_agent_dir");
    auto sp = build_system_prompt(base + "/a/b", files);
    CHECK(contains(sp, "<project_context>"));
    CHECK(contains(sp, "</project_context>"));
    CHECK(contains(sp, "ROOT-RULES"));
    CHECK(contains(sp, "LEAF-RULES"));
    CHECK(contains(sp, "<project_instructions path=\""));
    CHECK(contains(sp, "Current working directory:"));
    // Root rules appear before leaf rules.
    CHECK(sp.find("ROOT-RULES") < sp.find("LEAF-RULES"));
}

TEST_CASE("system prompt without context files has no project block") {
    auto sp = build_system_prompt("/tmp", {});
    CHECK_FALSE(contains(sp, "<project_context>"));
    CHECK(contains(sp, "Current date:"));
}
