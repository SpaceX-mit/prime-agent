// libs/coding/include/pi_coding/tools/write_tool.hpp
#pragma once

#include "pi_agent/tool.hpp"

namespace pi::coding::tools {

class WriteTool : public pi::agent::Tool {
public:
    explicit WriteTool(std::string cwd);
    std::string name() const override { return "write"; }
    std::string label() const override { return "Write file"; }
    std::string description() const override {
        return "Write content to a file (overwrites if exists). "
               "Arguments: path (string, required), content (string, required).";
    }
    pi::core::Json parameters() const override;
    pi::agent::ToolResult execute(
        const pi::core::Json& args,
        pi::agent::AbortSignal& signal,
        pi::agent::ProgressFn on_update) override;

private:
    std::string cwd_;
};

}  // namespace pi::coding::tools
