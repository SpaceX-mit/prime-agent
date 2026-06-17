// libs/coding/include/pi_coding/tools/bash_tool.hpp
#pragma once

#include "pi_agent/tool.hpp"

namespace pi::coding::tools {

class BashTool : public pi::agent::Tool {
public:
    BashTool();
    std::string name() const override { return "bash"; }
    std::string label() const override { return "Run shell command"; }
    std::string description() const override {
        return "Run a shell command. Returns stdout+stderr. Output is truncated if too long. "
               "Arguments: command (string, required), timeout (number, ms, default 120000).";
    }
    pi::core::Json parameters() const override;
    pi::agent::ToolResult execute(
        const pi::core::Json& args,
        pi::agent::AbortSignal& signal,
        pi::agent::ProgressFn on_update) override;

    /// Max output size returned to the LLM; excess is truncated.
    static constexpr size_t kMaxOutputBytes = 30'000;
};

}  // namespace pi::coding::tools
