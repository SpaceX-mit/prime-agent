// libs/coding/src/tools/web_search.cpp
#include "pi_coding/tools/web_search_tool.hpp"
#include "pi_core/env.hpp"
#include "pi_core/json.hpp"
#include "pi_http/http_client.hpp"
#include "pi_http/http_types.hpp"
#include <sstream>

namespace pi::coding::tools {

pi::core::Json WebSearchTool::parameters() const {
    return {{"type","object"},{"properties",{
        {"query",{{"type","string"},{"description","Search query"}}},
        {"count", {{"type","number"},{"description","Number of results (default 5)"}}}
    }},{"required",{"query"}}};
}

pi::agent::ToolResult WebSearchTool::execute(
    const pi::core::Json& args, pi::agent::AbortSignal&, pi::agent::ProgressFn) {

    std::string query = args.value("query", std::string{});
    int count = static_cast<int>(args.value("count", 5.0));
    if (count < 1) count = 1;
    if (count > 10) count = 10;
    if (query.empty()) {
        pi::agent::ToolResult r; r.is_error = true;
        r.content.push_back(pi::ai::TextContent{"error: query is required"});
        return r;
    }

    pi::http::HttpClient client;
    pi::http::HttpRequest req;
    std::string api_key;
    if (auto k = pi::core::env::get("BRAVE_SEARCH_API_KEY")) api_key = *k;

    std::string output;

    if (!api_key.empty()) {
        // Brave Search API
        req.method = "GET";
        req.url    = "https://api.search.brave.com/res/v1/web/search";
        req.query["q"]     = query;
        req.query["count"] = std::to_string(count);
        req.headers["Accept"]                = "application/json";
        req.headers["Accept-Encoding"]       = "gzip";
        req.headers["X-Subscription-Token"]  = api_key;
        req.timeout_ms = 15'000;
        auto res = client.send(req);
        if (res && res.value().ok()) {
            auto j = pi::core::tryParse(res.value().body);
            if (j && j->contains("web") && (*j)["web"].contains("results")) {
                int n = 0;
                for (auto& r : (*j)["web"]["results"]) {
                    if (n++ >= count) break;
                    output += "**" + r.value("title", std::string{}) + "**\n";
                    output += r.value("url", std::string{}) + "\n";
                    output += r.value("description", std::string{}) + "\n\n";
                }
            }
        }
    } else {
        // DuckDuckGo instant answers (no API key, limited results)
        req.method = "GET";
        req.url    = "https://api.duckduckgo.com/";
        req.query["q"]    = query;
        req.query["format"] = "json";
        req.query["no_html"] = "1";
        req.headers["User-Agent"] = "pi-agent/1.0";
        req.timeout_ms = 15'000;
        auto res = client.send(req);
        if (res && res.value().ok()) {
            auto j = pi::core::tryParse(res.value().body);
            if (j) {
                std::string abstract = j->value("AbstractText", std::string{});
                std::string source   = j->value("AbstractSource", std::string{});
                std::string abstract_url = j->value("AbstractURL", std::string{});
                if (!abstract.empty()) {
                    output += "**" + source + "**\n" + abstract_url + "\n" + abstract + "\n\n";
                }
                if (j->contains("RelatedTopics")) {
                    int n = 0;
                    for (auto& t : (*j)["RelatedTopics"]) {
                        if (n++ >= count) break;
                        std::string text = t.value("Text", std::string{});
                        std::string url  = t.contains("FirstURL") ? t["FirstURL"].get<std::string>() : "";
                        if (!text.empty()) output += url + "\n" + text + "\n\n";
                    }
                }
            }
        }
    }

    pi::agent::ToolResult r;
    if (output.empty()) output = "(no results found for: " + query + ")";
    r.content.push_back(pi::ai::TextContent{output});
    return r;
}

}  // namespace pi::coding::tools
