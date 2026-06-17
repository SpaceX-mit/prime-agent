// libs/coding/src/tools/read.cpp
#include "pi_coding/tools/read_tool.hpp"

#include "pi_core/file_io.hpp"
#include "pi_core/path.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace pi::coding::tools {

namespace {
std::string detect_image_mime(const std::string& path) {
    auto ext = [&] {
        auto dot = path.find_last_of('.');
        return dot == std::string::npos ? std::string() : path.substr(dot + 1);
    }();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (ext == "png")  return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif")  return "image/gif";
    if (ext == "webp") return "image/webp";
    if (ext == "bmp")  return "image/bmp";
    return "";
}

bool is_image_mime(const std::string& m) {
    return m.rfind("image/", 0) == 0;
}
}  // namespace

ReadTool::ReadTool(std::string cwd) : cwd_(std::move(cwd)) {}

pi::core::Json ReadTool::parameters() const {
    return pi::core::Json{
        {"type", "object"},
        {"properties", {
            {"path", {{"type", "string"}, {"description", "File path to read"}}},
            {"offset", {{"type", "number"}, {"description", "Line offset (0-based)"}}},
            {"limit", {{"type", "number"}, {"description", "Max lines to read"}}}
        }},
        {"required", {"path"}}
    };
}

pi::agent::ToolResult ReadTool::execute(
    const pi::core::Json& args,
    pi::agent::AbortSignal& /*signal*/,
    pi::agent::ProgressFn /*on_update*/) {

    pi::agent::ToolResult r;
    if (!args.contains("path") || !args["path"].is_string()) {
        pi::ai::TextContent t; t.text = "Error: missing required argument 'path'";
        r.content = {t}; r.is_error = true; return r;
    }

    std::string rel_path = args["path"].get<std::string>();
    std::string path = pi::core::path::is_within(cwd_, rel_path)
                          ? rel_path
                          : (rel_path[0] == '/' ? rel_path : pi::core::path::join(cwd_, rel_path));

    auto bytes = pi::core::file::read_bytes(path);
    if (!bytes) {
        pi::ai::TextContent t; t.text = "Error: " + bytes.error().to_string();
        r.content = {t}; r.is_error = true; return r;
    }

    // Image?
    std::string mime = detect_image_mime(path);
    if (is_image_mime(mime) && bytes.value().size() < 10 * 1024 * 1024) {
        // Return image content.
        pi::ai::ImageContent img;
        img.mime_type = mime;
        // base64 encode
        static const char* kAlpha =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string enc;
        enc.reserve(((bytes.value().size() + 2) / 3) * 4);
        size_t i = 0;
        while (i + 3 <= bytes.value().size()) {
            uint32_t v = (bytes.value()[i] << 16) | (bytes.value()[i+1] << 8) | bytes.value()[i+2];
            enc += kAlpha[(v >> 18) & 0x3f];
            enc += kAlpha[(v >> 12) & 0x3f];
            enc += kAlpha[(v >> 6) & 0x3f];
            enc += kAlpha[v & 0x3f];
            i += 3;
        }
        if (i + 1 == bytes.value().size()) {
            uint32_t v = bytes.value()[i] << 16;
            enc += kAlpha[(v >> 18) & 0x3f];
            enc += kAlpha[(v >> 12) & 0x3f];
            enc += '=';
            enc += '=';
        } else if (i + 2 == bytes.value().size()) {
            uint32_t v = (bytes.value()[i] << 16) | (bytes.value()[i+1] << 8);
            enc += kAlpha[(v >> 18) & 0x3f];
            enc += kAlpha[(v >> 12) & 0x3f];
            enc += kAlpha[(v >> 6) & 0x3f];
            enc += '=';
        }
        img.data = std::move(enc);
        r.content = {img};
        r.details = pi::core::Json{{"path", path}, {"sizeBytes", bytes.value().size()}};
        return r;
    }

    // Text — split by lines, apply offset/limit if provided.
    std::string text(reinterpret_cast<const char*>(bytes.value().data()), bytes.value().size());
    std::vector<std::string> lines;
    {
        std::string cur;
        for (char c : text) {
            if (c == '\n') { lines.push_back(std::move(cur)); cur.clear(); }
            else cur.push_back(c);
        }
        if (!cur.empty()) lines.push_back(std::move(cur));
    }
    size_t offset = args.value("offset", size_t{0});
    size_t limit = args.value("limit", size_t{200});
    if (offset > lines.size()) offset = lines.size();
    size_t end = std::min(lines.size(), offset + limit);

    std::ostringstream o;
    o << "File: " << path << "\n";
    o << "Lines " << offset << "-" << end << " of " << lines.size() << "\n";
    o << "---\n";
    for (size_t i = offset; i < end; ++i) {
        o << (i + 1) << "\t" << lines[i] << "\n";
    }

    pi::ai::TextContent t; t.text = o.str();
    r.content = {t};
    r.details = pi::core::Json{{"path", path}, {"totalLines", lines.size()},
                                {"start", offset}, {"end", end}};
    return r;
}

}  // namespace pi::coding::tools
