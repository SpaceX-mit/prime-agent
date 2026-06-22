// libs/coding/src/tools/fetch.cpp
#include "pi_coding/tools/fetch_tool.hpp"
#include "pi_http/http_client.hpp"
#include "pi_http/http_types.hpp"

namespace pi::coding::tools {

pi::core::Json FetchTool::parameters() const {
    return {
        {"type", "object"},
        {"properties", {
            {"url",      {{"type", "string"}, {"description", "URL to fetch"}}},
            {"maxBytes", {{"type", "number"}, {"description", "Max response bytes (default 50000)"}}}
        }},
        {"required", {"url"}}
    };
}

pi::agent::ToolResult FetchTool::execute(
    const pi::core::Json& args,
    pi::agent::AbortSignal& /*signal*/,
    pi::agent::ProgressFn /*on_update*/) {

    std::string url = args.value("url", std::string{});
    size_t max_bytes = static_cast<size_t>(args.value("maxBytes", 50000.0));
    if (url.empty()) {
        pi::agent::ToolResult r;
        r.content.push_back(pi::ai::TextContent{"error: url is required"});
        r.is_error = true;
        return r;
    }

    pi::http::HttpClient client;
    pi::http::HttpRequest req;
    req.method = "GET";
    req.url = url;
    req.headers["User-Agent"] = "pi-agent/1.0";
    req.timeout_ms = 30'000;
    req.connect_timeout_ms = 10'000;

    auto res = client.send(req);
    pi::agent::ToolResult r;
    if (!res) {
        r.content.push_back(pi::ai::TextContent{"error: " + res.error().to_string()});
        r.is_error = true;
        return r;
    }
    auto& resp = res.value();
    if (!resp.ok()) {
        r.content.push_back(pi::ai::TextContent{
            "HTTP " + std::to_string(resp.status) + " " + resp.status_text
            + (resp.body.empty() ? "" : "\n" + resp.body.substr(0, 500))});
        r.is_error = true;
        return r;
    }
    std::string body = resp.body;
    bool truncated = body.size() > max_bytes;
    if (truncated) body.resize(max_bytes);
    r.content.push_back(pi::ai::TextContent{
        (truncated ? "[truncated to " + std::to_string(max_bytes) + " bytes]\n" : "") + body});
    return r;
}

}  // namespace pi::coding::tools
