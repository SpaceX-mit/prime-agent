// libs/coding/include/pi_coding/tools/web_search_tool.hpp
#pragma once
#include "pi_agent/tool.hpp"
namespace pi::coding::tools {
class WebSearchTool : public pi::agent::Tool {
public:
    std::string name()  const override { return "web_search"; }
    std::string label() const override { return "Search the web"; }
    std::string description() const override {
        return "Search the web for information. Returns snippets from search results. "
               "Arguments: query (string, required), count (number, default 5 results). "
               "Set BRAVE_SEARCH_API_KEY env var for Brave Search; falls back to DuckDuckGo.";
    }
    pi::core::Json parameters() const override;
    pi::agent::ToolResult execute(const pi::core::Json& args, pi::agent::AbortSignal&,
                                  pi::agent::ProgressFn) override;
};
}
