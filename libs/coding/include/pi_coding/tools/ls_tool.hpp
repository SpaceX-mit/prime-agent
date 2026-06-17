// libs/coding/include/pi_coding/tools/ls_tool.hpp
#pragma once

#include "pi_agent/tool.hpp"

namespace pi::coding::tools {

class LsTool : public pi::agent::Tool {
public:
    explicit LsTool(std::string cwd);
    std::string name() const override { return "ls"; }
    std::string label() const override { return "List directory"; }
    std::string description() const override {
        return "List entries in a directory. "
               "Arguments: path (string, default cwd), all (boolean, default false).";
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
