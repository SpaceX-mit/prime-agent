// libs/coding/include/pi_coding/context_files.hpp
// Project context files (AGENTS.md / CLAUDE.md) + system-prompt assembly.
//
// Mirrors upstream pi's resource-loader.ts loadProjectContextFiles() +
// system-prompt.ts injection: load the global agent-dir context file first,
// then walk from cwd up to root collecting per-directory context files
// (ancestors ordered root→cwd), dedup by path. The collected files are
// injected into the system prompt inside a <project_context> block.
#pragma once

#include <string>
#include <vector>

namespace pi::coding {

struct ContextFile {
    std::string path;
    std::string content;
};

/// Load project context files. Order: global ($PI_AGENT_DIR or ~/.pi/agent)
/// first, then ancestors from filesystem root down to `cwd`. For each
/// directory the first existing candidate of AGENTS.md / AGENTS.MD /
/// CLAUDE.md / CLAUDE.MD wins. Paths are de-duplicated.
std::vector<ContextFile> load_project_context_files(const std::string& cwd,
                                                    const std::string& agent_dir = "");

/// Assemble the full system prompt: the base coding-agent instructions, an
/// optional <project_context> block built from `context_files`, and the
/// current date + working directory footer (matching upstream system-prompt.ts).
std::string build_system_prompt(const std::string& cwd,
                                const std::vector<ContextFile>& context_files);

}  // namespace pi::coding
