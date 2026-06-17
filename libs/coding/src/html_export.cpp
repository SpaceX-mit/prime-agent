// libs/coding/src/html_export.cpp
// Standalone HTML export: inlines CSS + minimal JS, no external assets.

#include "pi_coding/html_export.hpp"
#include "pi_coding/session_manager.hpp"

#include "pi_core/file_io.hpp"
#include "pi_core/json.hpp"
#include "pi_core/strutil.hpp"

#include <chrono>
#include <ctime>
#include <fstream>
#include <sstream>

namespace pi::coding {

namespace {

std::string escape_html(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += c;
        }
    }
    return out;
}

std::string render_text_block(std::string_view text) {
    std::ostringstream o;
    o << "<pre class=\"text\">" << escape_html(text) << "</pre>";
    return o.str();
}

std::string render_tool_call(const pi::core::Json& tc) {
    std::ostringstream o;
    std::string name = tc.value("name", std::string{"tool"});
    std::string id = tc.value("id", std::string{});
    std::string args_str = tc.value("arguments", std::string{});
    if (args_str.empty()) args_str = tc.value("arguments", pi::core::Json::object()).dump();
    o << "<details class=\"tool-call\"><summary>"
      << "<span class=\"tool-icon\">⚙</span> "
      << "<b>" << escape_html(name) << "</b>"
      << "<span class=\"tool-id\">" << escape_html(id) << "</span></summary>";
    o << "<div class=\"tool-args\">";
    o << "<b>Arguments:</b><pre>" << escape_html(args_str) << "</pre>";
    o << "</div></details>";
    return o.str();
}

std::string render_message(const pi::core::Json& m, int index) {
    std::string role = m.value("role", std::string{"user"});
    std::ostringstream o;
    o << "<div class=\"message message-" << role << "\" data-index=\"" << index << "\">";
    o << "<div class=\"role-badge\">" << escape_html(role) << "</div>";
    o << "<div class=\"content\">";

    if (m.contains("content")) {
        auto& content = m["content"];
        if (content.is_string()) {
            o << render_text_block(content.get<std::string>());
        } else if (content.is_array()) {
            for (auto& c : content) {
                std::string ct = c.value("type", std::string{});
                if (ct == "text") {
                    o << render_text_block(c.value("text", std::string{}));
                } else if (ct == "toolCall") {
                    o << render_tool_call(c);
                } else if (ct == "thinking") {
                    o << "<details class=\"thinking\"><summary>Thinking</summary>"
                      << "<pre>" << escape_html(c.value("thinking", std::string{}))
                      << "</pre></details>";
                } else if (ct == "image") {
                    o << "<div class=\"image\">[" << escape_html(c.value("mimeType", std::string{"image"}))
                      << "]</div>";
                }
            }
        }
    }

    o << "</div></div>";
    return o.str();
}

const char* kStyle = R"(
:root {
  --bg: #0e0e10; --panel: #1a1a1c; --border: #2a2a2e;
  --text: #e4e4e7; --dim: #9ca3af; --accent: #60a5fa;
  --user: #38bdf8; --assistant: #a78bfa; --tool: #fbbf24;
}
* { box-sizing: border-box; }
body { margin: 0; font: 14px/1.5 -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
       background: var(--bg); color: var(--text); }
.container { max-width: 800px; margin: 0 auto; padding: 24px 16px; }
header { border-bottom: 1px solid var(--border); padding-bottom: 16px; margin-bottom: 24px; }
h1 { margin: 0 0 4px 0; font-size: 18px; }
.meta { color: var(--dim); font-size: 12px; }
.message { margin: 16px 0; padding: 12px 16px; border-left: 3px solid var(--border);
           background: var(--panel); border-radius: 4px; }
.message-user { border-left-color: var(--user); }
.message-assistant { border-left-color: var(--assistant); }
.message-toolResult { border-left-color: var(--tool); }
.role-badge { display: inline-block; font-size: 11px; padding: 2px 8px;
              border-radius: 3px; background: var(--border); color: var(--dim);
              text-transform: uppercase; margin-bottom: 8px; }
pre { background: #0a0a0b; border: 1px solid var(--border); padding: 12px;
      border-radius: 4px; overflow-x: auto; font-family: 'Menlo', 'Consolas', monospace;
      font-size: 12px; margin: 8px 0; white-space: pre-wrap; }
details.tool-call, details.thinking { margin: 8px 0; }
details summary { cursor: pointer; padding: 6px 0; }
.tool-icon { color: var(--tool); margin-right: 6px; }
.tool-id { color: var(--dim); font-size: 11px; margin-left: 8px; }
.tool-args { padding: 0 12px; }
.image { padding: 8px; background: var(--border); border-radius: 4px; text-align: center; }
footer { margin-top: 48px; padding-top: 16px; border-top: 1px solid var(--border);
         color: var(--dim); font-size: 12px; text-align: center; }
.toolbar { position: sticky; top: 0; background: var(--bg); padding: 8px 0;
           border-bottom: 1px solid var(--border); margin-bottom: 16px; }
.toolbar button { background: var(--panel); color: var(--text); border: 1px solid var(--border);
                  padding: 4px 12px; border-radius: 4px; cursor: pointer; font-size: 12px; }
.toolbar button:hover { background: var(--border); }
)";

const char* kScript = R"(
document.querySelectorAll('details.tool-call').forEach(d => {
  d.addEventListener('toggle', () => {
    if (d.open) {
      // Pre-render args as JSON if valid.
      const argsPre = d.querySelector('.tool-args pre');
      if (argsPre) {
        try {
          const parsed = JSON.parse(argsPre.textContent);
          argsPre.textContent = JSON.stringify(parsed, null, 2);
        } catch (e) { /* leave as-is */ }
      }
    }
  });
});
)";

}  // namespace

int export_session_html(const std::string& jsonl_path,
                        const std::string& html_path) {
    SessionManager sm(jsonl_path);
    auto header = sm.read_header();
    auto entries = sm.read_entries();

    std::ostringstream html;
    html << "<!DOCTYPE html>\n<html lang=\"en\"><head>\n";
    html << "<meta charset=\"utf-8\">\n";
    html << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";
    html << "<title>prime-agent session</title>\n";
    html << "<style>" << kStyle << "</style>\n";
    html << "</head><body><div class=\"container\">\n";

    html << "<header><h1>prime-agent session</h1>";
    if (header) {
        html << "<div class=\"meta\">";
        html << "id: <code>" << escape_html(header->id) << "</code>";
        html << " · " << escape_html(header->timestamp);
        html << " · cwd: <code>" << escape_html(header->cwd) << "</code>";
        html << "</div>";
    }
    html << "</header>\n";

    html << "<div class=\"toolbar\"><button onclick=\"window.print()\">Print</button></div>\n";

    int count = 0;
    for (auto& e : entries) {
        if (e.type == "message") {
            html << render_message(e.data["message"], count);
            count++;
        }
    }

    html << "<footer>Exported by prime-agent v0.1.0 · "
         << entries.size() << " entries · " << count << " messages</footer>\n";
    html << "</div><script>" << kScript << "</script></body></html>\n";

    auto r = pi::core::file::write_atomic(html_path, html.str());
    if (!r) return -1;
    return count;
}

}  // namespace pi::coding
