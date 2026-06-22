// libs/coding/include/pi_coding/tools/fetch_tool.hpp
#pragma once
#include "pi_agent/tool.hpp"

namespace pi::coding::tools {

class FetchTool : public pi::agent::Tool {
public:
    std::string name()  const override { return "fetch"; }
    std::string label() const override { return "Fetch URL"; }
    std::string description() const override {
        return "Fetch the contents of a URL and return the response body as text. "
               "Follows redirects, supports HTTPS. Arguments: url (string, required), "
               "maxBytes (number, optional, default 50000).";
    }
    pi::core::Json parameters() const override;
    pi::agent::ToolResult execute(
        const pi::core::Json& args,
        pi::agent::AbortSignal& signal,
        pi::agent::ProgressFn on_update) override;
};

}  // namespace pi::coding::tools
