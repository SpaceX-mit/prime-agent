// libs/coding/include/pi_coding/tools/edit_tool.hpp
#pragma once

#include "pi_agent/tool.hpp"

namespace pi::coding::tools {

class EditTool : public pi::agent::Tool {
public:
    explicit EditTool(std::string cwd);
    std::string name() const override { return "edit"; }
    std::string label() const override { return "Edit file"; }
    std::string description() const override {
        return "Edit a file by replacing oldString with newString. "
               "oldString must match exactly (or be unique when allOccurrences=false). "
               "Arguments: path (string, required), oldString (string, required), "
               "newString (string, required), allOccurrences (boolean, optional, default false).";
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
