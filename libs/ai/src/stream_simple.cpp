// libs/ai/src/stream_simple.cpp
#include "pi_ai/stream_simple.hpp"
#include "pi_ai/provider.hpp"
#include "pi_core/error.hpp"

#include <chrono>
#include <cctype>

namespace pi::ai {

namespace {

// Extract an HTTP status code from a provider error string of the shape
// "<provider>: HTTP <code> <text>: ...". Returns 0 if none is found.
int extract_http_status(const std::string& raw) {
    auto pos = raw.find("HTTP ");
    if (pos == std::string::npos) return 0;
    pos += 5;
    int code = 0;
    bool any = false;
    while (pos < raw.size() && std::isdigit(static_cast<unsigned char>(raw[pos]))) {
        code = code * 10 + (raw[pos] - '0');
        ++pos;
        any = true;
    }
    return any ? code : 0;
}

}  // namespace

std::string humanize_stream_error(const std::string& provider,
                                  const std::string& raw) {
    int status = extract_http_status(raw);
    std::string p = provider.empty() ? "the provider" : provider;
    switch (status) {
        case 401:
        case 403:
            return "authentication failed (" + std::to_string(status) +
                   ") — check your API key for " + p + ", or run /login";
        case 429:
            return "rate limited by " + p + " (429) — too many requests";
        case 400:
            return "bad request (400) — " + p + " rejected the input";
        case 404:
            return "model or endpoint not found (404) on " + p;
        case 500: case 502: case 503: case 504:
            return p + " server error (" + std::to_string(status) +
                   ") — temporary, usually resolves on retry";
        default: break;
    }
    // Transport-level (no HTTP status): timeouts, connection failures.
    auto lc = raw;
    for (auto& c : lc) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lc.find("timed out") != std::string::npos || lc.find("timeout") != std::string::npos)
        return "request to " + p + " timed out — check your connection";
    if (lc.find("connect") != std::string::npos &&
        (lc.find("fail") != std::string::npos || lc.find("refused") != std::string::npos))
        return "could not connect to " + p + " — check your network";
    // Fallback: trim and cap the raw message so we never dump huge JSON.
    std::string trimmed = raw;
    if (trimmed.size() > 200) trimmed = trimmed.substr(0, 197) + "…";
    return trimmed;
}

bool is_retryable_stream_error(const std::string& raw) {
    int status = extract_http_status(raw);
    if (status != 0) {
        // Retry on rate-limit, request-timeout, and 5xx server errors only.
        return status == 408 || status == 429 ||
               (status >= 500 && status <= 599);
    }
    // No HTTP status → transport-level failure (timeout, connection drop).
    // These are typically transient, so retry.
    auto lc = raw;
    for (auto& c : lc) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lc.find("timed out") != std::string::npos ||
        lc.find("timeout") != std::string::npos ||
        lc.find("connect") != std::string::npos ||
        lc.find("connection") != std::string::npos ||
        lc.find("stream ended unexpectedly") != std::string::npos ||
        lc.find("eof") != std::string::npos)
        return true;
    return false;
}

std::shared_ptr<EventStream> stream_simple(
    const Model& model,
    const Context& ctx,
    const SimpleStreamOptions& opts) {
    auto stream = std::make_shared<EventStream>();
    auto provider = ProviderRegistry::instance().get(model.provider);
    if (!provider) {
        AssistantMessage m;
        m.api = to_string(model.api);
        m.provider = model.provider;
        m.model = model.id;
        m.stop_reason = "error";
        m.error_message = "ai: no provider registered for: " + model.provider;
        m.timestamp = static_cast<int64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        stream->end(std::move(m));
        return stream;
    }

    // Spawn the streaming task on a background thread.
    std::thread([stream, provider, model, ctx, opts]() {
        try {
            auto sub = provider->stream_simple(model, ctx, opts);
            // Drain sub stream into our stream.
            // Use blocking pull() — try_pull() would race with the provider thread
            // and miss events pushed after our first poll.
            int drained = 0;
            while (auto ev = sub->pull()) {
                stream->push(std::move(*ev));
                drained++;
            }
            // sub->pull() returned nullopt → sub is finished. Now get final result.
            auto res = sub->result();
            if (res) stream->end(std::move(res.value()));
            else stream->end_with_error(res.error());
        } catch (const std::exception& ex) {
            AssistantMessage m;
            m.api = to_string(model.api);
            m.provider = model.provider;
            m.model = model.id;
            m.stop_reason = "error";
            m.error_message = std::string("ai: exception: ") + ex.what();
            stream->end(std::move(m));
        } catch (...) {
            AssistantMessage m;
            m.stop_reason = "error";
            m.error_message = "ai: unknown exception";
            stream->end(std::move(m));
        }
    }).detach();

    return stream;
}

AssistantMessage run_to_completion(
    const Model& model,
    const Context& ctx,
    const SimpleStreamOptions& opts) {
    auto stream = stream_simple(model, ctx, opts);
    auto res = stream->result();
    if (res) return std::move(res.value());
    AssistantMessage m;
    m.stop_reason = "error";
    m.error_message = res.error().to_string();
    return m;
}

}  // namespace pi::ai
