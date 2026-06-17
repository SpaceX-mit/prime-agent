// libs/coding/include/pi_coding/tools/find_tool.hpp
#pragma once

#include "pi_agent/tool.hpp"

namespace pi::coding::tools {

class FindTool : public pi::agent::Tool {
public:
    explicit FindTool(std::string cwd);
    std::string name() const override { return "find"; }
    std::string label() const override { return "Find files by glob"; }
    std::string description() const override {
        return "Find files matching a glob pattern (e.g. \"*.cpp\", \"**/*.h\"). "
               "Respects .gitignore. Arguments: pattern (string, required), "
               "path (string, default cwd), type (string: file|directory, default file), "
               "limit (number, default 200).";
    }
    pi::core::Json parameters() const override;
    pi::agent::ToolResult execute(
        const pi::core::Json& args,
        pi::agent::AbortSignal& signal,
        pi::agent::ProgressFn on_update) override;

    static constexpr size_t kMaxResults = 1000;

private:
    std::string cwd_;
};

}  // namespace pi::coding::tools
