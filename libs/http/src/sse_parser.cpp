// libs/http/src/sse_parser.cpp
#include "pi_http/sse_parser.hpp"

#include <string>

namespace pi::http {

SseParser::SseParser(std::function<void(const SseEvent&)> on_event)
    : on_event_(std::move(on_event)) {}

void SseParser::dispatch_event() {
    if (data_lines_.empty() && name_.empty() && id_.empty()) {
        // Pure comment or heartbeat — ignore.
        return;
    }
    SseEvent ev;
    ev.name = name_.empty() ? std::string("message") : name_;
    ev.id = id_;
    for (size_t i = 0; i < data_lines_.size(); ++i) {
        if (i) ev.data += '\n';
        ev.data += data_lines_[i];
    }
    if (on_event_) on_event_(ev);
    data_lines_.clear();
    name_.clear();
    // id is persistent; do not clear.
}

void SseParser::handle_line(std::string_view line) {
    // Comments start with ':'.
    if (!line.empty() && line[0] == ':') return;

    auto colon = line.find(':');
    std::string_view field, value;
    if (colon == std::string_view::npos) {
        field = line;
        value = std::string_view{};
    } else {
        field = line.substr(0, colon);
        value = line.substr(colon + 1);
        if (!value.empty() && value.front() == ' ') value.remove_prefix(1);
    }
    std::string f(field);
    if (f == "data") {
        data_lines_.emplace_back(value);
    } else if (f == "event") {
        name_.assign(value);
    } else if (f == "id") {
        id_.assign(value);
    } else if (f == "retry") {
        // not supported in V1
    } else if (f.empty()) {
        // Empty line dispatches the current event.
        dispatch_event();
    }
}

pi::core::Result<void> SseParser::feed(std::string_view chunk) {
    buffer_.append(chunk);
    while (true) {
        auto nl = buffer_.find('\n');
        if (nl == std::string::npos) break;
        std::string_view line(buffer_.data(), nl);
        // strip trailing CR
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        handle_line(line);
        buffer_.erase(0, nl + 1);
    }
    return pi::core::Result<void>::ok();
}

void SseParser::end_of_stream() {
    if (!buffer_.empty()) {
        std::string_view line(buffer_);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        handle_line(line);
        buffer_.clear();
    }
    dispatch_event();
}

}  // namespace pi::http
