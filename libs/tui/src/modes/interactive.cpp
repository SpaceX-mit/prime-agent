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
    std::string status;
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
                    std::string cwd) {

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

    // Session persistence: create a new session file under the default
    // sessions directory. Each user message, assistant message, tool
    // result, and compaction event is appended as a JSONL entry. This
    // mirrors upstream pi's interactive-mode session behavior.
    std::string session_path = coding::SessionManager::default_dir() + "/"
                              + coding::SessionManager::new_session_id() + ".jsonl";
    {
        coding::SessionManager sm(session_path);
        coding::SessionHeader hdr;
        hdr.id = sm.path().substr(sm.path().find_last_of('/') + 1,
                                   sm.path().size() - sm.path().find_last_of('/') - 6);
        hdr.timestamp = "2026-06-17T00:00:00Z";  // overwritten below
        // Use real timestamp.
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
        {
            std::lock_guard<std::mutex> g(state_mtx);
            turns = state.turns;
            streaming = state.streaming;
            current_text = state.current_text;
        }
        // Simulate render_chat() using snapshot.
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
        std::string joined;
        for (auto& l : lines) {
            joined += l;
            joined += '\n';
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
          << "\x1b[0m";
        std::lock_guard<std::mutex> g(state_mtx);
        state.turns.push_back(o.str());
        state.redraw_needed = true;
    };

    show_welcome();
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
                                state.current_text += aev.delta;
                                state.status = "streaming…";
                                redraw = true;
                            } else if (aev.kind == pi::ai::AssistantMessageEvent::Kind::ToolCallEnd) {
                                std::ostringstream o;
                                o << "\n" << theme.dim << "[tool: " << aev.tool_call.name << "]\x1b[0m\n";
                                state.current_text += o.str();
                                redraw = true;
                            } else if (aev.kind == pi::ai::AssistantMessageEvent::Kind::Done) {
                                state.status = "done";
                            }
                            break;
                        }
                        case pi::agent::AgentEvent::Kind::MessageEnd:
                            // Persist each finalized message (assistant or
                            // tool result) to the session JSONL.
                            append_message_entry_fn(ev.message);
                            break;
                        case pi::agent::AgentEvent::Kind::ToolExecutionEnd: {
                            std::ostringstream o;
                            o << theme.dim << "← " << ev.tool_name
                              << (ev.tool_is_error ? " [error]\n" : " [ok]\n") << "\x1b[0m";
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
    while (!tui.should_quit()) {
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
            bool was_streaming = false;
            {
                std::lock_guard<std::mutex> g(state_mtx);
                was_streaming = state.streaming;
                if (state.abort) {
                    auto mutable_abort = std::dynamic_pointer_cast<pi::agent::MutableAbort>(state.abort);
                    if (mutable_abort) mutable_abort->signal();
                }
            }
            if (!was_streaming) {
                // Nothing to cancel; treat as quit.
                tui.quit();
                break;
            }
            // Otherwise let the agent thread observe the abort and wind down.
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
                    o << theme.accent << "› " << theme.primary << text << "\x1b[0m\n";
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
