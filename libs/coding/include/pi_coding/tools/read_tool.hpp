// libs/coding/include/pi_coding/tools/read_tool.hpp
#pragma once

#include "pi_agent/tool.hpp"

namespace pi::coding::tools {

class ReadTool : public pi::agent::Tool {
public:
    explicit ReadTool(std::string cwd);
    std::string name() const override { return "read"; }
    std::string label() const override { return "Read file"; }
    std::string description() const override {
        return "Read a file. Returns file content. Optionally limit with offset/limit. "
               "Detects image MIME and returns image content. "
               "Arguments: path (string, required), offset (number, optional), limit (number, optional).";
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
