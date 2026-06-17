// libs/coding/src/tools/write.cpp
#include "pi_coding/tools/write_tool.hpp"

#include "pi_core/file_io.hpp"
#include "pi_core/path.hpp"

namespace pi::coding::tools {

WriteTool::WriteTool(std::string cwd) : cwd_(std::move(cwd)) {}

pi::core::Json WriteTool::parameters() const {
    return pi::core::Json{
        {"type", "object"},
        {"properties", {
            {"path", {{"type", "string"}, {"description", "File path"}}},
            {"content", {{"type", "string"}, {"description", "Content to write"}}}
        }},
        {"required", {"path", "content"}}
    };
}

pi::agent::ToolResult WriteTool::execute(
    const pi::core::Json& args,
    pi::agent::AbortSignal& /*signal*/,
    pi::agent::ProgressFn /*on_update*/) {

    pi::agent::ToolResult r;
    if (!args.contains("path") || !args["path"].is_string()
        || !args.contains("content") || !args["content"].is_string()) {
        pi::ai::TextContent t; t.text = "Error: missing required arguments";
        r.content = {t}; r.is_error = true; return r;
    }
    std::string rel = args["path"].get<std::string>();
    std::string path = (rel[0] == '/') ? rel : pi::core::path::join(cwd_, rel);
    std::string content = args["content"].get<std::string>();

    auto w = pi::core::file::write_atomic(path, content);
    if (!w) {
        pi::ai::TextContent t; t.text = "Error: " + w.error().to_string();
        r.content = {t}; r.is_error = true; return r;
    }
    pi::ai::TextContent t;
    t.text = "wrote " + std::to_string(content.size()) + " bytes to " + path;
    r.content = {t};
    r.details = pi::core::Json{{"path", path}, {"bytesWritten", content.size()}};
    return r;
}

}  // namespace pi::coding::tools
