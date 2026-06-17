// libs/coding/src/tools/edit.cpp
#include "pi_coding/tools/edit_tool.hpp"

#include "pi_core/file_io.hpp"
#include "pi_core/path.hpp"

#include <string>

namespace pi::coding::tools {

EditTool::EditTool(std::string cwd) : cwd_(std::move(cwd)) {}

pi::core::Json EditTool::parameters() const {
    return pi::core::Json{
        {"type", "object"},
        {"properties", {
            {"path", {{"type", "string"}, {"description", "File path"}}},
            {"oldString", {{"type", "string"}, {"description", "Exact string to find"}}},
            {"newString", {{"type", "string"}, {"description", "Replacement"}}},
            {"allOccurrences", {{"type", "boolean"},
                                 {"description", "Replace every match (default false)"}}}
        }},
        {"required", {"path", "oldString", "newString"}}
    };
}

pi::agent::ToolResult EditTool::execute(
    const pi::core::Json& args,
    pi::agent::AbortSignal& /*signal*/,
    pi::agent::ProgressFn /*on_update*/) {

    pi::agent::ToolResult r;
    if (!args.contains("path") || !args["path"].is_string()
        || !args.contains("oldString") || !args["oldString"].is_string()
        || !args.contains("newString") || !args["newString"].is_string()) {
        pi::ai::TextContent t; t.text = "Error: missing required arguments";
        r.content = {t}; r.is_error = true; return r;
    }
    std::string rel = args["path"].get<std::string>();
    std::string path = (rel[0] == '/') ? rel : pi::core::path::join(cwd_, rel);
    std::string old_s = args["oldString"].get<std::string>();
    std::string new_s = args["newString"].get<std::string>();
    bool all = args.value("allOccurrences", false);

    auto rd = pi::core::file::read(path);
    if (!rd) {
        pi::ai::TextContent t; t.text = "Error: " + rd.error().to_string();
        r.content = {t}; r.is_error = true; return r;
    }
    std::string content = std::move(rd.value());

    auto count_matches = [&](const std::string& s) {
        size_t c = 0, pos = 0;
        while ((pos = s.find(old_s, pos)) != std::string::npos) { ++c; pos += old_s.size(); }
        return c;
    };

    size_t matches = count_matches(content);

    if (matches == 0) {
        pi::ai::TextContent t; t.text = "Error: oldString not found in " + path;
        r.content = {t}; r.is_error = true; return r;
    }
    if (!all && matches > 1) {
        pi::ai::TextContent t; t.text = "Error: oldString matches " + std::to_string(matches)
                                       + " places; set allOccurrences=true to replace all";
        r.content = {t}; r.is_error = true; return r;
    }

    std::string out;
    out.reserve(content.size());
    size_t pos = 0;
    while (true) {
        auto hit = content.find(old_s, pos);
        if (hit == std::string::npos) {
            out.append(content.substr(pos));
            break;
        }
        out.append(content.substr(pos, hit - pos));
        out.append(new_s);
        pos = hit + old_s.size();
        if (!all) {
            out.append(content.substr(pos));
            break;
        }
    }

    auto w = pi::core::file::write_atomic(path, out);
    if (!w) {
        pi::ai::TextContent t; t.text = "Error: " + w.error().to_string();
        r.content = {t}; r.is_error = true; return r;
    }
    pi::ai::TextContent t;
    t.text = "edited " + path + " (replaced " + std::to_string(all ? matches : 1)
             + " occurrence" + (matches == 1 ? "" : "s") + ")";
    r.content = {t};
    r.details = pi::core::Json{{"path", path}, {"replaced", all ? matches : 1}};
    return r;
}

}  // namespace pi::coding::tools
