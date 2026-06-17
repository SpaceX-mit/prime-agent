// libs/coding/include/pi_coding/tools/grep_tool.hpp
#pragma once

#include "pi_agent/tool.hpp"

namespace pi::coding::tools {

class GrepTool : public pi::agent::Tool {
public:
    explicit GrepTool(std::string cwd);
    std::string name() const override { return "grep"; }
    std::string label() const override { return "Search file contents"; }
    std::string description() const override {
        return "Search file contents with a regex pattern. Returns matching lines "
               "(file:line:content). Use 'include' to filter by glob (e.g. \"*.cpp\"). "
               "Arguments: pattern (string, required), path (string, default cwd), "
               "include (string, optional, glob), limit (number, default 100).";
    }
    pi::core::Json parameters() const override;
    pi::agent::ToolResult execute(
        const pi::core::Json& args,
        pi::agent::AbortSignal& signal,
        pi::agent::ProgressFn on_update) override;

    static constexpr size_t kMaxMatches = 500;

private:
    std::string cwd_;
};

}  // namespace pi::coding::tools
