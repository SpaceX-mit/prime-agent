// libs/tui/include/pi_tui/modes/interactive.hpp
// Interactive mode: input → agent loop → output.

#pragma once

#include "pi_ai/models.hpp"
#include "pi_agent/agent_loop.hpp"
#include "pi_coding/tools/bash_tool.hpp"
#include "pi_coding/tools/edit_tool.hpp"
#include "pi_coding/tools/read_tool.hpp"
#include "pi_coding/tools/write_tool.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace pi::tui::modes {

/// InteractiveMode renders a prompt input, streams agent responses.
/// If `resume_path` is non-empty, the session file at that path is
/// loaded: prior messages seed history (full multi-turn context) and
/// new entries are appended to the same file. Empty `resume_path`
/// starts a fresh session under the default sessions directory.
/// Returns 0 on clean exit, non-zero on error.
/// `key_resolver(provider)` returns an API key for the given provider name;
/// used by the /model command to switch models at runtime.
int run_interactive(const pi::ai::Model& model,
                    pi::ai::SimpleStreamOptions opts,
                    std::string cwd,
                    std::string resume_path = "",
                    std::string system_prompt = "",
                    std::function<std::string(const std::string&)> key_resolver = {});

}  // namespace pi::tui::modes
