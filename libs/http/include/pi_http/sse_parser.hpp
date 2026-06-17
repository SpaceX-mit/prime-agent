// libs/http/include/pi_http/sse_parser.hpp
// Minimal Server-Sent Events (SSE) parser per HTML5 spec.
#pragma once

#include "pi_core/error.hpp"
#include "pi_core/result.hpp"

#include <functional>
#include <string>
#include <string_view>

namespace pi::http {

/// A complete SSE event: one or more `data:` lines + optional event name and id.
struct SseEvent {
    std::string name;       // defaults to "message"
    std::string data;       // joined with "\n" between consecutive data lines
    std::string id;         // optional
    std::string raw;        // raw multi-line bytes for debugging
};

/// Streaming SSE parser. Feed bytes via `feed`; emit events via callback.
class SseParser {
public:
    explicit SseParser(std::function<void(const SseEvent&)> on_event);
    ~SseParser() = default;

    /// Feed a chunk of bytes. May emit 0, 1, or many events.
    /// Returns false (via Result) only on internal error (very rare).
    pi::core::Result<void> feed(std::string_view chunk);

    /// End of stream: emit any pending event whose last line had no trailing newline.
    void end_of_stream();

    const pi::core::Error& error() const { return err_; }

private:
    std::function<void(const SseEvent&)> on_event_;
    std::string buffer_;     // current line being assembled
    std::vector<std::string> data_lines_;
    std::string name_;
    std::string id_;
    pi::core::Error err_;

    void dispatch_event();
    void handle_line(std::string_view line);
};

}  // namespace pi::http
