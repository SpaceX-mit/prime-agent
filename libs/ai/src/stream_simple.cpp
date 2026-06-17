// libs/ai/src/stream_simple.cpp
#include "pi_ai/stream_simple.hpp"
#include "pi_ai/provider.hpp"
#include "pi_core/error.hpp"

#include <chrono>

namespace pi::ai {

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
            while (auto ev = sub->try_pull()) {
                stream->push(std::move(*ev));
            }
            // Drain remaining via result().
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
