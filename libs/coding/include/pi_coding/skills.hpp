// libs/coding/include/pi_coding/skills.hpp
// Agent Skills: discover SKILL.md files, parse their frontmatter, and format
// an <available_skills> block for the system prompt. Mirrors upstream pi's
// skills.ts: a skill is a directory containing SKILL.md whose YAML
// frontmatter carries `name`, `description`, and optional
// `disable-model-invocation`. Skills are loaded from the global agent dir
// ($PI_AGENT_DIR or ~/.pi/agent)/skills and the project's .pi/skills.
#pragma once

#include <string>
#include <vector>

namespace pi::coding {

struct Skill {
    std::string name;
    std::string description;
    std::string file_path;   // absolute path to the SKILL.md
    std::string base_dir;    // directory containing SKILL.md
    bool disable_model_invocation = false;
};

/// Load all skills visible for `cwd`: global agent-dir skills first, then
/// project (.pi/skills under cwd). Invalid skills (missing/oversized name or
/// description, bad name charset) are skipped. Names are de-duplicated
/// (first wins).
std::vector<Skill> load_skills(const std::string& cwd,
                               const std::string& agent_dir = "");

/// Build the <available_skills> system-prompt block (empty string if there
/// are no model-invocable skills). Matches upstream formatSkillsForPrompt().
std::string format_skills_for_prompt(const std::vector<Skill>& skills);

}  // namespace pi::coding
