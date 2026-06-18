// libs/tui/src/modes/interactive.cpp
// Interactive mode: a minimal but functional REPL.
//
// INC-002 fix: the agent loop runs on a detached background thread so
// the main TUI loop stays responsive — keystrokes (including Ctrl-C
// to abort) are processed in real-time while the model streams.

#include "pi_tui/modes/interactive.hpp"

#include "pi_agent/agent_loop.hpp"
#include "pi_ai/stream_simple.hpp"
#include "pi_coding/auth_storage.hpp"
#include "pi_coding/compaction.hpp"
#include "pi_coding/oauth.hpp"
#include "pi_coding/session_manager.hpp"
#include "pi_core/ansi.hpp"
#include "pi_core/log.hpp"
#include "pi_core/path.hpp"
#include "pi_core/strutil.hpp"
#include "pi_tui/components/box.hpp"
#include "pi_tui/components/footer.hpp"
#include "pi_tui/components/input.hpp"
#include "pi_tui/components/text.hpp"
#include "pi_tui/terminal.hpp"
#include "pi_tui/theme.hpp"
#include "pi_tui/think_filter.hpp"
#include "pi_tui/tui.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <typeinfo>

namespace pi::tui::modes {

namespace {

/// Mutable agent-loop state shared between the main loop and the
/// background agent thread.
struct ChatState {
    std::vector<std::string> turns;  // each turn is text+markdown lines
    std::string current_text;
    bool streaming = false;
    // Filter for inline <think>...</think> tags emitted by reasoning models
    // (MiniMax, DeepSeek). Reset on every new agent run.
    ThinkFilter think;
    std::string status;
    // Scroll viewport: how many lines from the *bottom* of the full chat
    // we are currently anchored above. 0 = stuck to bottom (auto-follow).
    // Positive = user has scrolled up by that many lines.
    int scroll_back = 0;
    bool follow = true;       // auto-stick to bottom while streaming
    std::vector<pi::ai::Message> history;     // full conversation (for /compact, /tree)
    pi::coding::SessionManager* session = nullptr;
    std::string model_id;
    int compaction_count = 0;

    // Set by the agent thread after every state change so the main loop
    // knows to redraw.
    bool redraw_needed = true;

    // Set by the main loop (Ctrl-C) and read by the agent thread.
    std::shared_ptr<pi::agent::AbortSignal> abort;
};

std::vector<std::string> render_chat(const ChatState& s, const Theme& t, int width) {
    std::vector<std::string> out;
    for (auto& turn : s.turns) {
        std::istringstream iss(turn);
        std::string line;
        while (std::getline(iss, line)) {
            out.push_back(t.primary + line + "\x1b[0m");
        }
        out.push_back("");
    }
    if (s.streaming) {
        std::istringstream iss(s.current_text);
        std::string line;
        while (std::getline(iss, line)) {
            out.push_back(t.accent + line + "\x1b[0m");
        }
    }
    return out;
}

}  // namespace

int run_interactive(const pi::ai::Model& model,
                    pi::ai::SimpleStreamOptions opts,
                    std::string cwd,
                    std::string resume_path) {

    Terminal term;
    if (!Terminal::is_tty()) {
        std::cerr << "interactive mode requires a TTY\n";
        return 1;
    }

    Theme theme = Theme::dark();
    term.enter_raw_mode();

    TUI tui(term, theme);

    auto input = std::make_shared<components::Input>(theme);
    input->set_prompt(theme.accent + "› " + theme.primary);
    auto footer = std::make_shared<components::Footer>(theme, "interactive", model.id);
    auto chat_box = std::make_shared<components::Box>(components::Box::Vertical, 0);
    (void)chat_box;

    auto chat_text = std::make_shared<components::Text>("", theme.primary);

    auto root = std::make_shared<components::Box>(components::Box::Vertical, 0);
    root->add(chat_text);
    root->add(input);
    root->add(footer);
    tui.set_root(root);

    ChatState state;

    // Session persistence: either resume an existing JSONL session (replay
    // its messages into history + transcript) or create a fresh one. In
    // both cases, new entries are appended to the same file so the next
    // resume sees the full continued conversation.
    std::string session_path;
    std::vector<pi::ai::Message> resumed_messages;
    if (!resume_path.empty()) {
        session_path = resume_path;
        coding::SessionManager sm(session_path);
        auto hdr = sm.read_header();
        if (!hdr) {
            std::cerr << "error: cannot read session header at " << session_path << "\n";
            return 2;
        }
        auto entries = sm.read_entries();
        auto ctx = coding::build_session_context(*hdr, entries, false);
        resumed_messages = std::move(ctx.messages);
        state.session = new coding::SessionManager(session_path);
    } else {
        session_path = coding::SessionManager::default_dir() + "/"
                      + coding::SessionManager::new_session_id() + ".jsonl";
        coding::SessionManager sm(session_path);
        coding::SessionHeader hdr;
        hdr.id = sm.path().substr(sm.path().find_last_of('/') + 1,
                                   sm.path().size() - sm.path().find_last_of('/') - 6 - 1);
        std::time_t t = std::time(nullptr);
        std::tm tm{};
        gmtime_r(&t, &tm);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
        hdr.timestamp = buf;
        hdr.cwd = cwd;
        sm.initialize(hdr);
        state.session = new coding::SessionManager(session_path);
    }
    // Helper: serialize any Message variant to JSON.
    auto message_to_json = [](const pi::ai::Message& m) -> pi::core::Json {
        return std::visit([](auto& v) -> pi::core::Json {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, pi::ai::UserMessage>) {
                return v.to_json();
            } else if constexpr (std::is_same_v<T, pi::ai::AssistantMessage>) {
                return v.to_json();
            } else {
                // ToolResultMessage — synthesize JSON.
                pi::core::Json j;
                j["role"] = "toolResult";
                j["toolCallId"] = v.tool_call_id;
                j["toolName"] = v.tool_name;
                j["isError"] = v.is_error;
                pi::core::Json arr = pi::core::Json::array();
                for (auto& c : v.content) {
                    if (std::holds_alternative<pi::ai::TextContent>(c)) {
                        arr.push_back({{"type", "text"},
                                        {"text", std::get<pi::ai::TextContent>(c).text}});
                    }
                }
                j["content"] = arr;
                return j;
            }
        }, m);
    };

    // Helper: append a message entry to the session.
    auto append_message_entry = [&](const pi::ai::Message& m) {
        if (!state.session) return;
        coding::SessionEntry e;
        e.type = "message";
        e.data["message"] = message_to_json(m);
        state.session->append_entry(e);
    };

    // Single mutex protects ALL of ChatState. Reads in render_chat are
    // outside the lock; the background agent thread holds it only while
    // mutating state.
    std::mutex state_mtx;

    auto refresh_chat = [&]() {
        // Snapshot under the lock, render outside it.
        std::vector<std::string> lines;
        bool streaming;
        std::string current_text;
        std::vector<std::string> turns;
        int scroll_back;
        bool follow;
        {
            std::lock_guard<std::mutex> g(state_mtx);
            turns = state.turns;
            streaming = state.streaming;
            current_text = state.current_text;
            scroll_back = state.scroll_back;
            follow = state.follow;
        }
        for (auto& turn : turns) {
            std::istringstream iss(turn);
            std::string line;
            while (std::getline(iss, line)) {
                lines.push_back(theme.primary + line + "\x1b[0m");
            }
            lines.push_back("");
        }
        if (streaming) {
            std::istringstream iss(current_text);
            std::string line;
            while (std::getline(iss, line)) {
                lines.push_back(theme.accent + line + "\x1b[0m");
            }
        }
        // Viewport: terminal rows minus 2 (input + footer).
        auto sz = term.size();
        int rows = sz.first;
        int chat_rows = std::max(1, rows - 2);
        int total = static_cast<int>(lines.size());
        int start = 0;
        if (total > chat_rows) {
            if (follow) {
                start = total - chat_rows;
            } else {
                int max_back = total - chat_rows;
                int back = std::min(scroll_back, max_back);
                start = max_back - back;
            }
        }
        int end = std::min(total, start + chat_rows);

        std::string joined;
        int emitted = 0;
        for (int i = start; i < end; ++i) {
            if (emitted) joined += '\n';
            joined += lines[i];
            emitted++;
        }
        // Pad with blank lines so input + footer stay anchored at the bottom
        // even when chat is short. Each iteration adds exactly one line.
        for (int i = emitted; i < chat_rows; ++i) {
            if (i) joined += '\n';
        }
        // Scroll indicator if not at bottom (replaces last line, not adds).
        if (!follow && total > chat_rows) {
            // Append on its own line — we already have chat_rows lines, so
            // overwrite the last padding line.
            // ponytail: indicator may push input down by 1 if chat is full;
            // upgrade path: reserve a dedicated row for it.
            joined += theme.dim;
            joined += "  ↑ scrolled — End to follow\x1b[0m";
        }
        chat_text->set_text(joined);
    };

    auto show_welcome = [&]() {
        std::ostringstream o;
        o << theme.dim << "Welcome to prime-agent (interactive mode).\n"
          << "Type your message and press Enter. /help for commands, /exit to quit.\n"
          << "Tools: bash, read, write, edit. Model: " << model.id << "\n"
          << "\x1b[0m";
        std::lock_guard<std::mutex> g(state_mtx);
        state.turns.push_back(o.str());
        state.redraw_needed = true;
    };

    auto help_text = [&]() {
        std::ostringstream o;
        o << theme.dim
          << "Commands:\n"
          << "  /exit, /quit       Quit the session\n"
          << "  /clear             Clear the screen\n"
          << "  /help              Show this help\n"
          << "  /model <id>        Switch model (e.g. /model openai/gpt-4o-mini)\n"
          << "  /new               Start a fresh conversation\n"
          << "  /sessions          List saved sessions (id · timestamp · #msgs · cwd)\n"
          << "  /resume <id>       Restart pi resuming the given session id (prefix ok)\n"
          << "  /continue          Restart pi resuming the most recent session\n"
          << "  /history           Dump current state.history (diagnostic)\n"
          << "\x1b[0m";
        std::lock_guard<std::mutex> g(state_mtx);
        state.turns.push_back(o.str());
        state.redraw_needed = true;
    };

    show_welcome();

    // Replay resumed messages into transcript + history so /compact, the
    // next prompt, and on-screen scrollback all see prior context.
    if (!resumed_messages.empty()) {
        std::lock_guard<std::mutex> g(state_mtx);
        state.history = resumed_messages;
        for (auto& m : resumed_messages) {
            std::ostringstream o;
            std::visit([&](auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, pi::ai::UserMessage>) {
                    o << theme.user_label << "› " << theme.primary;
                    for (auto& c : v.content) {
                        if (std::holds_alternative<pi::ai::TextContent>(c))
                            o << std::get<pi::ai::TextContent>(c).text;
                    }
                    o << "\x1b[0m\n";
                } else if constexpr (std::is_same_v<T, pi::ai::AssistantMessage>) {
                    for (auto& c : v.content) {
                        if (std::holds_alternative<pi::ai::TextContent>(c))
                            o << theme.primary << std::get<pi::ai::TextContent>(c).text << "\x1b[0m";
                        else if (std::holds_alternative<pi::ai::ToolCall>(c))
                            o << theme.dim << "[tool: "
                              << std::get<pi::ai::ToolCall>(c).name << "]\x1b[0m";
                    }
                    o << "\n";
                } else {
                    o << theme.dim << "← " << v.tool_name
                      << (v.is_error ? " [error]" : " [ok]") << "\x1b[0m\n";
                }
            }, m);
            if (!o.str().empty()) state.turns.push_back(o.str());
        }
        std::ostringstream o;
        o << theme.dim << "(resumed " << resumed_messages.size()
          << " messages from " << session_path << ")\n\x1b[0m";
        state.turns.push_back(o.str());
        state.redraw_needed = true;
    }
    refresh_chat();
    tui.render();

    // Spawn the agent loop on a detached background thread so the main
    // TUI loop keeps polling keys. This is INC-002's fix — the previous
    // synchronous `stream->drain(...)` blocked the main loop for the
    // entire duration of the agent run, making keystrokes impossible.
    auto spawn_agent = [&](std::vector<pi::ai::Message> messages,
                            pi::agent::AgentLoopConfig cfg_in) {
        // Replace with a stateful abort so Ctrl-C from the main loop
        // can cancel the agent mid-run.
        auto abort_signal = std::make_shared<pi::agent::MutableAbort>();
        cfg_in.signal = abort_signal;

        {
            std::lock_guard<std::mutex> g(state_mtx);
            state.streaming = true;
            state.current_text.clear();
            state.think = ThinkFilter{};
            state.status = "thinking…";
            state.abort = abort_signal;
            state.redraw_needed = true;
        }
        footer->set_status("thinking…");
        refresh_chat();
        tui.render();

        std::thread([&state_mtx, &state, &tui, &refresh_chat, &footer, &theme,
                      messages = std::move(messages), cfg = std::move(cfg_in),
                      append_message_entry_fn = append_message_entry]() mutable {
            auto stream = pi::agent::run_agent_loop(std::move(messages), std::move(cfg));
            std::vector<pi::ai::Message> final_messages;
            stream->drain([&](const pi::agent::AgentEvent& ev) {
                bool redraw = false;
                std::string status_msg;
                {
                    std::lock_guard<std::mutex> g(state_mtx);
                    switch (ev.kind) {
                        case pi::agent::AgentEvent::Kind::MessageUpdate: {
                            auto& aev = ev.assistant_event;
                            if (aev.kind == pi::ai::AssistantMessageEvent::Kind::TextDelta) {
                                // Wrap reasoning blocks with a labeled prefix so it
                                // visually reads as a distinct region (pi's pattern:
                                // italic + dedicated color + spacer above/below).
                                std::string enter = std::string("\n") + theme.thinking
                                    + "▎ reasoning ";
                                std::string leave = "\x1b[0m\n";
                                state.think.feed(aev.delta, enter, leave, state.current_text);
                                state.status = state.think.in_think ? "thinking…" : "streaming…";
                                redraw = true;
                            } else if (aev.kind == pi::ai::AssistantMessageEvent::Kind::ThinkingDelta) {
                                state.current_text += theme.thinking + aev.delta + "\x1b[0m";
                                state.status = "thinking…";
                                redraw = true;
                            } else if (aev.kind == pi::ai::AssistantMessageEvent::Kind::ToolCallEnd) {
                                std::ostringstream o;
                                o << "\n" << theme.tool_pending << "⏵ "
                                  << aev.tool_call.name << "\x1b[0m\n";
                                state.current_text += o.str();
                                redraw = true;
                            } else if (aev.kind == pi::ai::AssistantMessageEvent::Kind::Done) {
                                // INC-006: do NOT silently drop the final
                                // assistant text. Save it so MessageEnd can
                                // attach it to current_text. Done is sent
                                // before MessageEnd so the assistant may add
                                // a trailing newline via MessageEnd path.
                                status_msg = "done";
                            }
                            break;
                        }
                        case pi::agent::AgentEvent::Kind::MessageEnd: {
                            // INC-006: surface assistant messages (text,
                            // errors, aborts) to the user — previously we
                            // only persisted to JSONL and silently dropped
                            // the content. Compare upstream pi which calls
                            // streamingComponent.updateContent() and then
                            // surfaces errorMessage when stopReason is
                            // "error" / "aborted".
                            if (std::holds_alternative<pi::ai::AssistantMessage>(ev.message)) {
                                const auto& am = std::get<pi::ai::AssistantMessage>(ev.message);
                                // Extract final text from content blocks.
                                std::string final_text;
                                for (auto& c : am.content) {
                                    if (std::holds_alternative<pi::ai::TextContent>(c)) {
                                        final_text += std::get<pi::ai::TextContent>(c).text;
                                    }
                                }
                                // INC-006 fix appended final_text always; that
                                // double-printed when TextDelta events already
                                // streamed it. Only fall back to MessageEnd's
                                // final_text when the stream produced nothing
                                // (provider sent no deltas, or only a final).
                                if (!final_text.empty() && state.current_text.empty()) {
                                    state.current_text += final_text;
                                }
                                // Show error / abort messages in red.
                                if (am.stop_reason == "error" ||
                                    am.stop_reason == "aborted") {
                                    std::ostringstream o;
                                    o << "\n" << theme.error
                                      << "[" << am.stop_reason << ": "
                                      << (am.error_message ? *am.error_message : "(no detail)")
                                      << "]\x1b[0m\n";
                                    state.current_text += o.str();
                                    status_msg = am.stop_reason;
                                }
                                redraw = true;  // surface final text / error
                                // Usage → footer status (e.g. "in 1234 / out 567 tok").
                                if (am.usage.is_object()) {
                                    int in_tok = am.usage.value("inputTokens",
                                                                am.usage.value("input_tokens", 0));
                                    int out_tok = am.usage.value("outputTokens",
                                                                 am.usage.value("output_tokens", 0));
                                    if (in_tok || out_tok) {
                                        std::ostringstream u;
                                        u << "in " << in_tok << " · out " << out_tok << " tok";
                                        status_msg = u.str();
                                    }
                                }
                            }
                            // Persist every finalized message (assistant or
                            // tool result) to the session JSONL.
                            append_message_entry_fn(ev.message);
                            break;
                        }
                        case pi::agent::AgentEvent::Kind::ToolExecutionStart: {
                            std::string preview;
                            if (ev.tool_args.is_object()) {
                                if (ev.tool_args.contains("command"))
                                    preview = ev.tool_args["command"].get<std::string>();
                                else if (ev.tool_args.contains("path"))
                                    preview = ev.tool_args["path"].get<std::string>();
                                else if (ev.tool_args.contains("pattern"))
                                    preview = ev.tool_args["pattern"].get<std::string>();
                                else
                                    preview = ev.tool_args.dump();
                            }
                            if (preview.size() > 80) preview = preview.substr(0, 77) + "…";
                            std::ostringstream o;
                            o << theme.tool_pending << "  ↪ " << ev.tool_name;
                            if (!preview.empty()) o << " (" << preview << ")";
                            o << "\x1b[0m\n";
                            state.current_text += o.str();
                            redraw = true;
                            break;
                        }
                        case pi::agent::AgentEvent::Kind::ToolExecutionEnd: {
                            std::ostringstream o;
                            const std::string& c = ev.tool_is_error ? theme.tool_err : theme.tool_ok;
                            const char* sym = ev.tool_is_error ? "✗" : "✓";
                            std::string first;
                            for (auto& cc : ev.tool_result.content) {
                                if (std::holds_alternative<pi::ai::TextContent>(cc)) {
                                    const auto& t = std::get<pi::ai::TextContent>(cc).text;
                                    auto nl = t.find('\n');
                                    first = (nl == std::string::npos) ? t : t.substr(0, nl);
                                    break;
                                }
                            }
                            if (first.size() > 100) first = first.substr(0, 97) + "…";
                            o << c << "  " << sym << " " << ev.tool_name;
                            if (!first.empty()) o << theme.dim << "  " << first;
                            o << "\x1b[0m\n";
                            state.current_text += o.str();
                            redraw = true;
                            break;
                        }
                        case pi::agent::AgentEvent::Kind::AgentEnd:
                            // Persist the full conversation history so the
                            // next prompt starts with multi-turn context.
                            state.history = ev.messages;
                            final_messages = ev.messages;
                            break;
                        default:
                            break;
                    }
                    state.redraw_needed = true;
                }
                if (redraw) {
                    refresh_chat();
                    tui.render();
                }
                if (!status_msg.empty()) footer->set_status(status_msg);
            });

            // Agent thread is done. Move current_text into turns.
            {
                std::lock_guard<std::mutex> g(state_mtx);
                state.streaming = false;
                state.turns.push_back(state.current_text);
                state.current_text.clear();
                state.turns.push_back("");  // blank line separator
                state.status.clear();
                state.redraw_needed = true;
                (void)final_messages;
            }
            footer->set_status("");
            refresh_chat();
            tui.render();
        }).detach();
    };

    // Main TUI loop.
    auto stream_started = std::chrono::steady_clock::now();
    int spin_tick = 0;
    bool last_streaming = false;
    while (!tui.should_quit()) {
        // Liveness heartbeat: while streaming, refresh the footer every
        // ~250ms with a spinner + elapsed seconds so the user knows the
        // agent is alive even when the model hasn't sent a delta yet
        // (HTTP first-byte latency, long bash tools).
        bool is_streaming;
        {
            std::lock_guard<std::mutex> g(state_mtx);
            is_streaming = state.streaming;
        }
        if (is_streaming) {
            if (!last_streaming) {
                stream_started = std::chrono::steady_clock::now();
                last_streaming = true;
            }
            if (spin_tick % 5 == 0) {
                static const char* kFrames[] = {"⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧","⠇","⠏"};
                int sec = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - stream_started).count();
                std::ostringstream u;
                u << kFrames[(spin_tick / 5) % 10] << "  " << sec << "s";
                footer->set_status(u.str());
                tui.render();
            }
            spin_tick++;
        } else {
            last_streaming = false;
            spin_tick = 0;
        }

        // Decide whether to redraw (cheap, just a bool check).
        bool need_redraw = false;
        {
            std::lock_guard<std::mutex> g(state_mtx);
            if (state.redraw_needed) {
                state.redraw_needed = false;
                need_redraw = true;
            }
        }
        if (need_redraw) {
            refresh_chat();
            tui.render();
        }

        // Read keys with short timeout so we keep polling the redraw flag.
        auto k = term.try_read_key(50);
        if (!k) continue;

        if (k->kind == pi::tui::KeyEvent::Kind::CtrlC) {
            // Two-press semantics: first Ctrl-C aborts the in-flight agent
            // run; a second Ctrl-C within 2s force-quits even if the agent
            // is still winding down (e.g. waiting on a slow socket read).
            static auto last_ctrlc = std::chrono::steady_clock::time_point{};
            auto now = std::chrono::steady_clock::now();
            bool double_press = last_ctrlc.time_since_epoch().count() != 0 &&
                std::chrono::duration_cast<std::chrono::seconds>(now - last_ctrlc).count() < 2;
            last_ctrlc = now;

            bool was_streaming = false;
            {
                std::lock_guard<std::mutex> g(state_mtx);
                was_streaming = state.streaming;
                if (state.abort) {
                    auto mutable_abort = std::dynamic_pointer_cast<pi::agent::MutableAbort>(state.abort);
                    if (mutable_abort) mutable_abort->signal();
                }
            }
            if (!was_streaming || double_press) {
                tui.quit();
                break;
            }
            // First Ctrl-C while streaming: signal abort, keep TUI alive
            // so the user sees the wind-down (and can hit Ctrl-C again
            // if the wind-down hangs on a stuck socket read).
            footer->set_status("aborting (Ctrl-C again to force quit)…");
            tui.render();
            continue;
        }

        // Scrolling controls. Captured here so they don't reach the input
        // (which would treat PgUp/PgDn as garbage characters).
        if (k->kind == pi::tui::KeyEvent::Kind::PageUp ||
            k->kind == pi::tui::KeyEvent::Kind::PageDown ||
            k->kind == pi::tui::KeyEvent::Kind::End) {
            auto sz = term.size();
            int page = std::max(1, sz.first - 2 - 1);
            std::lock_guard<std::mutex> g(state_mtx);
            if (k->kind == pi::tui::KeyEvent::Kind::PageUp) {
                state.follow = false;
                state.scroll_back += page;
            } else if (k->kind == pi::tui::KeyEvent::Kind::PageDown) {
                state.scroll_back = std::max(0, state.scroll_back - page);
                if (state.scroll_back == 0) state.follow = true;
            } else {  // End
                state.follow = true;
                state.scroll_back = 0;
            }
            state.redraw_needed = true;
            continue;
        }

        if (input->on_key(*k)) {
            // INC-004: input mutated its text — must re-render the screen
            // or the user won't see what they typed (mirror upstream
            // Editor's tui.requestRender() that fires on any change).
            // render() is idempotent (no-op if frame unchanged) so the
            // cost is bounded.
            tui.render();
            if (input->take_submit()) {
                std::string text = input->text();
                input->set_text("");
                input->push_history(text);
                std::string cmd(text);
                cmd = std::string(pi::core::str::trim(cmd));
                if (cmd.empty()) continue;

                // Slash commands.
                if (cmd == "/exit" || cmd == "/quit") {
                    tui.quit();
                    break;
                }
                if (cmd == "/clear") {
                    std::lock_guard<std::mutex> g(state_mtx);
                    state.turns.clear();
                    state.current_text.clear();
                    state.redraw_needed = true;
                    continue;
                }
                if (cmd == "/help") {
                    help_text();
                    continue;
                }
                if (cmd == "/new") {
                    {
                        std::lock_guard<std::mutex> g(state_mtx);
                        state.turns.clear();
                        state.current_text.clear();
                        std::ostringstream o;
                        o << theme.dim << "(started new conversation)\n\x1b[0m";
                        state.turns.push_back(o.str());
                        state.redraw_needed = true;
                    }
                    continue;
                }
                if (cmd.rfind("/model ", 0) == 0) {
                    std::lock_guard<std::mutex> g(state_mtx);
                    state.turns.push_back(theme.dim +
                        "(model switch not yet implemented in V1: " + cmd.substr(7) + ")\x1b[0m\n");
                    state.redraw_needed = true;
                    continue;
                }
                if (cmd.rfind("/login ", 0) == 0 || cmd == "/login") {
                    std::string provider = cmd.size() > 6 ? cmd.substr(7) : "";
                    {
                        std::lock_guard<std::mutex> g(state_mtx);
                        if (provider.empty()) {
                            state.turns.push_back(theme.error +
                                "Usage: /login <provider>\n"
                                "Known: anthropic (Claude.ai OAuth — V2 framework only)\n"
                                "       For most providers, use --api-key or set API key env var.\n\x1b[0m");
                        } else if (provider != "anthropic") {
                            state.turns.push_back(theme.error +
                                "/login for '" + provider + "' is not yet wired in V2. "
                                "Use --api-key or env var instead.\n\x1b[0m");
                        }
                        state.redraw_needed = true;
                    }
                    refresh_chat();
                    tui.render();
                    continue;
                }
                if (cmd == "/compact") {
                    std::vector<pi::ai::Message> hist_copy;
                    {
                        std::lock_guard<std::mutex> g(state_mtx);
                        hist_copy = state.history;
                    }
                    if (hist_copy.empty()) {
                        std::lock_guard<std::mutex> g(state_mtx);
                        state.turns.push_back(theme.dim + "(no history to compact)\n\x1b[0m");
                        state.redraw_needed = true;
                        continue;
                    }
                    // Synchronous /compact is acceptable (LLM summary,
                    // not interactive).
                    footer->set_status("compacting…");
                    refresh_chat();
                    tui.render();

                    pi::coding::CompactionSettings settings;
                    auto compact_res = pi::coding::compact(
                        model, opts, hist_copy, settings);
                    {
                        std::lock_guard<std::mutex> g(state_mtx);
                        if (compact_res.aborted) {
                            state.turns.push_back(theme.error + "(compaction failed)\n\x1b[0m");
                        } else if (compact_res.drop_count == 0) {
                            state.turns.push_back(theme.dim + "(nothing to compact)\n\x1b[0m");
                        } else {
                            state.history = compact_res.kept_messages;
                            state.compaction_count++;
                            std::ostringstream o;
                            o << theme.dim << "[compacted " << compact_res.drop_count
                              << " earlier messages (" << state.compaction_count
                              << " compactions total)]\n\x1b[0m";
                            state.turns.push_back(o.str());
                            if (state.session) {
                                pi::coding::SessionEntry e;
                                e.type = "compaction";
                                e.data["summary"] = compact_res.summary;
                                e.data["droppedCount"] = compact_res.drop_count;
                                state.session->append_entry(e);
                            }
                        }
                        state.redraw_needed = true;
                    }
                    footer->set_status("");
                    refresh_chat();
                    tui.render();
                    continue;
                }
                if (cmd == "/history") {
                    std::vector<pi::ai::Message> hist_copy;
                    {
                        std::lock_guard<std::mutex> g(state_mtx);
                        hist_copy = state.history;
                    }
                    std::ostringstream o;
                    o << theme.dim << "[history: " << hist_copy.size() << " messages]\n";
                    for (size_t i = 0; i < hist_copy.size(); ++i) {
                        std::visit([&](auto& v) {
                            using T = std::decay_t<decltype(v)>;
                            o << "  #" << i << " ";
                            if constexpr (std::is_same_v<T, pi::ai::UserMessage>) {
                                o << "user: ";
                                for (auto& c : v.content)
                                    if (std::holds_alternative<pi::ai::TextContent>(c))
                                        o << std::get<pi::ai::TextContent>(c).text.substr(0, 80);
                            } else if constexpr (std::is_same_v<T, pi::ai::AssistantMessage>) {
                                o << "asst[" << v.stop_reason << "]: ";
                                for (auto& c : v.content) {
                                    if (std::holds_alternative<pi::ai::TextContent>(c))
                                        o << std::get<pi::ai::TextContent>(c).text.substr(0, 60);
                                    else if (std::holds_alternative<pi::ai::ToolCall>(c))
                                        o << "<tool:" << std::get<pi::ai::ToolCall>(c).name << ">";
                                }
                            } else {
                                o << "tool[" << v.tool_name << (v.is_error ? " err" : " ok") << "]";
                            }
                            o << "\n";
                        }, hist_copy[i]);
                    }
                    o << "\x1b[0m";
                    std::lock_guard<std::mutex> g(state_mtx);
                    state.turns.push_back(o.str());
                    state.redraw_needed = true;
                    continue;
                }
                if (cmd == "/sessions") {
                    auto all = coding::SessionManager::list_all();
                    std::lock_guard<std::mutex> g(state_mtx);
                    if (all.empty()) {
                        state.turns.push_back(theme.dim + "(no saved sessions)\n\x1b[0m");
                    } else {
                        std::ostringstream o;
                        o << theme.dim;
                        size_t n = std::min<size_t>(all.size(), 20);
                        for (size_t i = 0; i < n; ++i) {
                            auto& s = all[i];
                            o << s.id.substr(0, 12) << "  " << s.timestamp
                              << "  " << s.message_count << " msgs  " << s.cwd << "\n";
                        }
                        if (all.size() > n) o << "(" << (all.size() - n) << " more…)\n";
                        o << "\x1b[0m";
                        state.turns.push_back(o.str());
                    }
                    state.redraw_needed = true;
                    continue;
                }
                if (cmd == "/continue" || cmd.rfind("/resume", 0) == 0) {
                    std::string target;
                    if (cmd == "/continue") {
                        auto all = coding::SessionManager::list_all();
                        if (all.empty()) {
                            std::lock_guard<std::mutex> g(state_mtx);
                            state.turns.push_back(theme.error + "(no saved sessions)\n\x1b[0m");
                            state.redraw_needed = true;
                            continue;
                        }
                        target = all.front().path;
                    } else {
                        std::string arg = cmd.size() > 7 ? cmd.substr(7) : "";
                        arg = std::string(pi::core::str::trim(arg));
                        if (arg.empty()) {
                            std::lock_guard<std::mutex> g(state_mtx);
                            state.turns.push_back(theme.error + "Usage: /resume <session-id-prefix>\n\x1b[0m");
                            state.redraw_needed = true;
                            continue;
                        }
                        target = coding::SessionManager::resolve_id_prefix(arg);
                        if (target.empty()) {
                            std::lock_guard<std::mutex> g(state_mtx);
                            state.turns.push_back(theme.error +
                                "(no unique session matches '" + arg + "')\n\x1b[0m");
                            state.redraw_needed = true;
                            continue;
                        }
                    }
                    // In-place resume: drop current state, load target session,
                    // continue in same process. No re-exec.
                    coding::SessionManager sm(target);
                    auto hdr = sm.read_header();
                    if (!hdr) {
                        std::lock_guard<std::mutex> g(state_mtx);
                        state.turns.push_back(theme.error +
                            "(failed to read header of " + target + ")\n\x1b[0m");
                        state.redraw_needed = true;
                        continue;
                    }
                    auto entries = sm.read_entries();
                    auto ctx = coding::build_session_context(*hdr, entries, false);
                    {
                        std::lock_guard<std::mutex> g(state_mtx);
                        delete state.session;
                        state.session = new coding::SessionManager(target);
                        state.history = ctx.messages;
                        state.turns.clear();
                        state.current_text.clear();
                        // Replay messages into transcript.
                        for (auto& m : ctx.messages) {
                            std::ostringstream o;
                            std::visit([&](auto& v) {
                                using T = std::decay_t<decltype(v)>;
                                if constexpr (std::is_same_v<T, pi::ai::UserMessage>) {
                                    o << theme.user_label << "› " << theme.primary;
                                    for (auto& c : v.content) {
                                        if (std::holds_alternative<pi::ai::TextContent>(c))
                                            o << std::get<pi::ai::TextContent>(c).text;
                                    }
                                    o << "\x1b[0m\n";
                                } else if constexpr (std::is_same_v<T, pi::ai::AssistantMessage>) {
                                    for (auto& c : v.content) {
                                        if (std::holds_alternative<pi::ai::TextContent>(c))
                                            o << theme.primary << std::get<pi::ai::TextContent>(c).text << "\x1b[0m";
                                        else if (std::holds_alternative<pi::ai::ToolCall>(c))
                                            o << theme.dim << "[tool: "
                                              << std::get<pi::ai::ToolCall>(c).name << "]\x1b[0m";
                                    }
                                    o << "\n";
                                } else {
                                    o << theme.dim << "← " << v.tool_name
                                      << (v.is_error ? " [error]" : " [ok]") << "\x1b[0m\n";
                                }
                            }, m);
                            if (!o.str().empty()) state.turns.push_back(o.str());
                        }
                        std::ostringstream o;
                        o << theme.dim << "(resumed " << ctx.messages.size()
                          << " messages from " << target << ")\n\x1b[0m";
                        state.turns.push_back(o.str());
                        state.redraw_needed = true;
                    }
                    refresh_chat();
                    tui.render();
                    continue;
                }
                if (cmd == "/tree") {
                    std::lock_guard<std::mutex> g(state_mtx);
                    state.turns.push_back(theme.dim + "[session tree — V2: not yet wired into interactive mode]\n\x1b[0m");
                    state.redraw_needed = true;
                    continue;
                }

                // Block new submissions while a previous one is running.
                {
                    bool is_streaming;
                    std::shared_ptr<pi::agent::AbortSignal> prev_abort;
                    {
                        std::lock_guard<std::mutex> g(state_mtx);
                        is_streaming = state.streaming;
                        prev_abort = state.abort;
                    }
                    if (is_streaming) {
                        // Queue this as a "steering" message: append to
                        // current_text so the user sees it inline, and
                        // append to the agent's user messages once the
                        // current turn finishes. (V1 simplification:
                        // ignore for now — just notify and drop.)
                        std::lock_guard<std::mutex> g(state_mtx);
                        state.turns.push_back(theme.dim +
                            "(agent still running; your message was discarded. Ctrl-C to cancel.)\n\x1b[0m");
                        state.redraw_needed = true;
                        continue;
                    }
                }

                // Echo the user prompt.
                {
                    std::lock_guard<std::mutex> g(state_mtx);
                    std::ostringstream o;
                    o << theme.user_label << "› " << theme.primary << text << "\x1b[0m\n";
                    state.turns.push_back(o.str());
                    state.history.push_back(pi::ai::UserMessage{});
                    std::get<pi::ai::UserMessage>(state.history.back()).content.push_back(
                        pi::ai::TextContent{text});
                    state.redraw_needed = true;
                }

                // Build tools and run agent on background thread.
                std::vector<pi::agent::ToolPtr> tools;
                tools.push_back(std::make_shared<pi::coding::tools::BashTool>());
                tools.push_back(std::make_shared<pi::coding::tools::ReadTool>(cwd));
                tools.push_back(std::make_shared<pi::coding::tools::WriteTool>(cwd));
                tools.push_back(std::make_shared<pi::coding::tools::EditTool>(cwd));

                std::vector<pi::ai::Message> msgs;
                {
                    std::lock_guard<std::mutex> g(state_mtx);
                    msgs = state.history;
                }
                pi::agent::AgentLoopConfig cfg;
                cfg.model = model;
                cfg.tools = std::move(tools);
                cfg.stream_opts = opts;

                spawn_agent(std::move(msgs), std::move(cfg));
                // spawn_agent starts the background thread; the main loop
                // continues to poll keys at the next iteration.
            }
            continue;
        }
    }

    term.leave_raw_mode();
    std::cout << "bye.\n";
    return 0;
}

}  // namespace pi::tui::modes
