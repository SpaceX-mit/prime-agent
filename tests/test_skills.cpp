// tests/test_skills.cpp
// P2.3: Agent Skills discovery, frontmatter parsing, and prompt formatting.
#define TEST_FRAMEWORK_IMPLEMENT
#include "test_framework.hpp"

#include "pi_coding/skills.hpp"
#include "pi_core/file_io.hpp"

#include <string>
#include <unistd.h>

using namespace pi::coding;

static std::string tmp_dir() {
    return "/tmp/pi_skills_" + std::to_string(::getpid());
}

TEST_CASE("load_skills: discovers SKILL.md with valid frontmatter") {
    auto base = tmp_dir();
    pi::core::file::create_directories(base + "/skills/my-skill");
    pi::core::file::write_atomic(base + "/skills/my-skill/SKILL.md",
        "---\nname: my-skill\ndescription: Do the thing\n---\n# My Skill\n");
    auto skills = load_skills("/tmp", base);
    REQUIRE(skills.size() == 1);
    CHECK(skills[0].name == "my-skill");
    CHECK(skills[0].description == "Do the thing");
    CHECK_FALSE(skills[0].disable_model_invocation);
}

TEST_CASE("load_skills: invalid name rejected") {
    auto base = tmp_dir() + "2";
    pi::core::file::create_directories(base + "/skills/Bad Name");
    pi::core::file::write_atomic(base + "/skills/Bad Name/SKILL.md",
        "---\nname: Bad Name\ndescription: has spaces\n---\n");
    auto skills = load_skills("/tmp", base);
    CHECK(skills.empty());
}

TEST_CASE("load_skills: disable-model-invocation parsed") {
    auto base = tmp_dir() + "3";
    pi::core::file::create_directories(base + "/skills/hidden");
    pi::core::file::write_atomic(base + "/skills/hidden/SKILL.md",
        "---\nname: hidden\ndescription: internal only\ndisable-model-invocation: true\n---\n");
    auto skills = load_skills("/tmp", base);
    REQUIRE(skills.size() == 1);
    CHECK(skills[0].disable_model_invocation);
}

TEST_CASE("format_skills_for_prompt: hidden skills excluded, visible included") {
    std::vector<Skill> skills = {
        {"visible", "Visible task", "/p/v/SKILL.md", "/p/v", false},
        {"hidden",  "Hidden task",  "/p/h/SKILL.md", "/p/h", true},
    };
    auto s = format_skills_for_prompt(skills);
    CHECK(s.find("visible") != std::string::npos);
    CHECK(s.find("hidden") == std::string::npos);
    CHECK(s.find("<available_skills>") != std::string::npos);
    CHECK(s.find("<skill>") != std::string::npos);
}

TEST_CASE("format_skills_for_prompt: empty when all hidden") {
    std::vector<Skill> skills = {{"x", "y", "/p/SKILL.md", "/p", true}};
    CHECK(format_skills_for_prompt(skills).empty());
}
