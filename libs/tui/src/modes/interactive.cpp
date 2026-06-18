// libs/tui/src/modes/interactive.cpp
// Interactive mode: a "well-behaved CLI" REPL.
//
// Architecture (rewrite): chat is written directly to the main screen so
// it flows into the terminal's native scrollback (mouse wheel scrolls
// history, text persists after exit, selection/copy work). A DECSTBM
// scroll region locks the bottom 2 rows for the input line + status
// footer. The agent loop runs on a detached background thread; all stdout
// writes are serialized through a single mutex (Ui::mtx_) so the main
// loop's keyboard redraws and the agent thread's streamed output never
// interleave.
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
#include "pi_tui/components/input.hpp"
#include "pi_tui/message_render.hpp"
#include "pi_tui/render_util.hpp"
#include "pi_tui/terminal.hpp"
#include "pi_tui/theme.hpp"
#include "pi_tui/think_filter.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

namespace pi::tui::modes {

namespace {

// ---------------------------------------------------------------------------
// Ui: serializes all stdout writes and owns the DECSTBM bottom-lock layout.
//
// The terminal is split into:
//   rows 1 .. rows-2   scroll region (chat history, native scrollback)
//   row  rows-1        input line (prompt + editable text + cursor)
//   row  rows          footer (model · spinner Ns · in/out tokens)
//
// When the terminal is too short (< 5 rows) we fall back to "no-lock" mode:
// no scroll region, chat just prints at the cursor, the input line is drawn
// inline and the footer is suppressed. This keeps tiny terminals usable.
// ---------------------------------------------------------------------------
class Ui {
public:
    Ui(Terminal& term, const Theme& theme, std::shared_ptr<components::Input> input)
        : term_(term), theme_(theme), input_(std::move(input)) {}

    // (Re)compute geometry and install the scroll region. Called at startup
    // and on every resize. Clears the screen so the lock is clean.
    void setup() {
        std::lock_guard<std::mutex> g(mtx_);
        auto sz = term_.size();
        rows_ = sz.first;
        cols_ = sz.second;
        locked_ = rows_ >= 5;
        if (locked_) {
            int bottom = rows_ - 2;  // last scrollable row; rows-1 + rows are fixed
            term_.write(pi::core::ansi::clear_screen());          // \x1b[2J\x1b[H
            term_.write(pi::core::ansi::set_scroll_region(1, bottom));
            // Park the cursor at the bottom of the scroll region and record
            // it, so the first emit() restore lands inside the region.
            term_.write(pi::core::ansi::move_cursor(bottom, 1));
            term_.write(pi::core::ansi::save_cursor());           // DECSC
        } else {
            // No room to lock; reset any prior region.
            term_.write(pi::core::ansi::reset_scroll_region());
        }
        term_.flush();
        redraw_bottom_locked();
    }

    // Restore full-screen scrolling. Called before leaving raw mode (the
    // terminal also resets the region in leave_raw_mode(), but doing it here
    // first leaves the cursor on a fresh line below the chat).
    void teardown() {
        std::lock_guard<std::mutex> g(mtx_);
        if (locked_) {
            term_.write(pi::core::ansi::reset_scroll_region());
            // Move below the (now-unlocked) input/footer rows so the shell
            // prompt starts on a clean line.
            term_.write(pi::core::ansi::move_cursor(rows_, 1));
            term_.write("\r\n");
        }
        term_.flush();
    }

    // Append committed chat text to the scroll region. `text` may contain
    // its own newlines and ANSI styling; we ensure it ends on a fresh line.
    void emit(const std::string& text) {
        if (text.empty()) return;
        std::lock_guard<std::mutex> g(mtx_);
        // Raw mode disables OPOST, so a bare '\n' only moves the cursor DOWN
        // and keeps the column — multi-line content would stair-step to the
        // right. Translate every '\n' to "\r\n" here, the single write
        // chokepoint, so all renderers/call sites stay left-aligned without
        // each having to emit carriage returns.
        std::string out = crlf(text);
        if (locked_) {
            term_.write(pi::core::ansi::restore_cursor());  // DECRC → back into region
            term_.write(out);
            term_.write(pi::core::ansi::save_cursor());     // DECSC → remember new spot
        } else {
            term_.write(out);
        }
        term_.flush();
        redraw_bottom_locked();
    }

    // Redraw the input line and footer in place (public, takes the lock).
    void redraw_bottom() {
        std::lock_guard<std::mutex> g(mtx_);
        redraw_bottom_locked();
    }

    void set_footer(const std::string& s) {
        std::lock_guard<std::mutex> g(mtx_);
        footer_ = s;
        redraw_bottom_locked();
    }

    // Update token counters shown in the footer.
    void set_tokens(int in_tok, int out_tok) {
        std::lock_guard<std::mutex> g(mtx_);
        in_tok_ = in_tok;
        out_tok_ = out_tok;
    }

    void set_model(const std::string& m) {
        std::lock_guard<std::mutex> g(mtx_);
        model_ = m;
    }

    // Current usable width (columns) for message rendering.
    int width() {
        std::lock_guard<std::mutex> g(mtx_);
        return cols_;
    }

private:
    // Translate bare '\n' to "\r\n" (raw mode has OPOST off). Idempotent:
    // an existing "\r\n" is left as-is.
    static std::string crlf(const std::string& s) {
        std::string out;
        out.reserve(s.size() + 16);
        for (size_t i = 0; i < s.size(); ++i) {
            char c = s[i];
            if (c == '\n' && (i == 0 || s[i - 1] != '\r')) out.push_back('\r');
            out.push_back(c);
        }
        return out;
    }

    // Compact token formatting, matching upstream footer.ts formatTokens():
    // <1k → "123", <10k → "1.2k", <1M → "123k", <10M → "1.2M", else "123M".
    static std::string fmt_tokens(long n) {
        char b[32];
        if (n < 1000) { snprintf(b, sizeof(b), "%ld", n); }
        else if (n < 10000) { snprintf(b, sizeof(b), "%.1fk", n / 1000.0); }
        else if (n < 1000000) { snprintf(b, sizeof(b), "%ldk", (long)((n + 500) / 1000)); }
        else if (n < 10000000) { snprintf(b, sizeof(b), "%.1fM", n / 1000000.0); }
        else { snprintf(b, sizeof(b), "%ldM", (long)((n + 500000) / 1000000)); }
        return b;
    }

    // Compose the footer (stats) line, mirroring upstream footer.ts layout:
    //   left:  [status/spinner] ↑<in> ↓<out>
    //   right: <model>  (right-aligned)
    // Whole line is dim. Caller must hold mtx_.
    std::string footer_text_() const {
        // Left side: live status (spinner / "streaming…") then token stats.
        std::string left;
        if (!footer_.empty()) left += footer_;
        if (in_tok_ || out_tok_) {
            if (!left.empty()) left += "  ";
            left += "\xE2\x86\x91" + fmt_tokens(in_tok_);   // ↑in
            left += " \xE2\x86\x93" + fmt_tokens(out_tok_); // ↓out
        }
        std::string right = model_;

        int lw = render::visible_width(left);
        int rw = render::visible_width(right);
        const int min_pad = 2;
        std::string line;
        if (lw + min_pad + rw <= cols_) {
            int pad = cols_ - lw - rw;
            line = left + std::string(pad < min_pad ? min_pad : pad, ' ') + right;
        } else {
            // Not enough room: truncate the right (model) side.
            int avail = cols_ - lw - min_pad;
            if (avail > 0) {
                std::string r = render::truncate_to_width(right, avail, "");
                int pad = cols_ - lw - render::visible_width(r);
                line = left + std::string(pad < 0 ? 0 : pad, ' ') + r;
            } else {
                line = render::truncate_to_width(left, cols_, "");
            }
        }
        // Dim the whole line (truecolor dim fg + reset).
        return theme_.dim + line + "\x1b[0m";
    }

    // Caller must hold mtx_.
    void redraw_bottom_locked() {
        if (locked_) {
            // Input line on row rows-1, footer on row rows.
            std::string in_line = input_->render(cols_).front();
            term_.write(pi::core::ansi::move_cursor(rows_ - 1, 1));
            term_.write(pi::core::ansi::clear_line());
            term_.write(in_line);
            term_.write(pi::core::ansi::move_cursor(rows_, 1));
            term_.write(pi::core::ansi::clear_line());
            term_.write(footer_text_());
            // Park the cursor back inside the scroll region so the next
            // emit() restore is correct even if no emit happens first.
            term_.write(pi::core::ansi::restore_cursor());
        } else {
            // Fallback: redraw the input line in place on the current row.
            std::string in_line = input_->render(cols_).front();
            term_.write("\r");
            term_.write(pi::core::ansi::clear_line());
            term_.write(in_line);
        }
        term_.flush();
    }

    Terminal& term_;
    const Theme& theme_;
    std::shared_ptr<components::Input> input_;
    std::mutex mtx_;
    int rows_ = 24;
    int cols_ = 80;
    bool locked_ = false;
    std::string footer_;
    std::string model_;
    int in_tok_ = 0;
    int out_tok_ = 0;
};

/// Mutable agent-loop state shared between the main loop and the
/// background agent thread. Chat history itself lives in the terminal's
/// scrollback (we write it out and forget it); this struct only holds the
/// machine state needed to drive the loop.
struct ChatState {
    bool streaming = false;
    // Accumulates the assistant's text for the current turn so MessageEnd
    // can fall back to it if the provider sent no streaming deltas.
    std::string current_text;
    // Filter for inline <think>...</think> tags emitted by reasoning models
    // (MiniMax, DeepSeek). Reset on every new agent run.
    ThinkFilter think;
    std::vector<pi::ai::Message> history;     // full conversation (for /compact, /tree)
    pi::coding::SessionManager* session = nullptr;
    int compaction_count = 0;

    // Set by the main loop (Ctrl-C) and read by the agent thread.
    std::shared_ptr<pi::agent::AbortSignal> abort;
};

// Render one resumed/replayed message to a chat block (used on resume and in
// the replay paths). Mirrors the streamed formatting via the shared message
// renderers so resumed history looks identical to live output.
std::string render_message(const pi::ai::Message& m, const Theme& theme, int width) {
    std::ostringstream o;
    std::visit([&](auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, pi::ai::UserMessage>) {
            std::string text;
            for (auto& c : v.content)
                if (std::holds_alternative<pi::ai::TextContent>(c))
                    text += std::get<pi::ai::TextContent>(c).text;
            o << msg::user_message(text, theme, width) << "\n";
        } else if constexpr (std::is_same_v<T, pi::ai::AssistantMessage>) {
            for (auto& c : v.content) {
                if (std::holds_alternative<pi::ai::TextContent>(c))
                    o << msg::assistant_text(std::get<pi::ai::TextContent>(c).text, theme, width) << "\n";
                else if (std::holds_alternative<pi::ai::ToolCall>(c))
                    o << msg::tool_execution(std::get<pi::ai::ToolCall>(c).name, "",
                                             msg::ToolState::Success, theme, width) << "\n";
            }
        } else {
            o << msg::tool_execution(v.tool_name, v.is_error ? "error" : "ok",
                                     v.is_error ? msg::ToolState::Error : msg::ToolState::Success,
                                     theme, width) << "\n";
        }
    }, m);
    return o.str();
}

}  // namespace

int run_interactive(const pi::ai::Model& model,
                    pi::ai::SimpleStreamOptions opts,
                    std::string cwd,
                    std::string resume_path,
                    std::string system_prompt) {

    Terminal term;
    if (!Terminal::is_tty()) {
        std::cerr << "interactive mode requires a TTY\n";
        return 1;
    }

    Theme theme = Theme::dark();
    term.enter_raw_mode();

    auto input = std::make_shared<components::Input>(theme);
    input->set_prompt(theme.accent + "› " + theme.primary);

    Ui ui(term, theme, input);
    ui.set_model(model.id);

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
            term.leave_raw_mode();
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

    // Single mutex protects ChatState's machine fields touched by both the
    // main loop and the agent thread (streaming flag, abort, history,
    // current_text). All stdout writes go through Ui's own mutex.
    std::mutex state_mtx;

    // Welcome banner straight into scrollback.
    {
        std::ostringstream o;
        o << theme.dim << "Welcome to prime-agent (interactive mode).\n"
          << "Type your message and press Enter. /help for commands, /exit to quit.\n"
          << "Tools: bash, read, write, edit. Model: " << model.id << "\n"
          << "\x1b[0m";
        ui.emit(o.str());
    }

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
          << "  /resume <id>       Resume the given session id (prefix ok)\n"
          << "  /continue          Resume the most recent session\n"
          << "  /compact           Summarize earlier messages to free context\n"
          << "  /login <provider>  Configure provider authentication\n"
          << "  /tree              Session branch view (not yet wired)\n"
          << "  /history           Dump current state.history (diagnostic)\n"
          << "\x1b[0m";
        ui.emit(o.str());
    };

    ui.setup();

    // Replay resumed messages into the transcript + history so /compact, the
    // next prompt, and on-screen scrollback all see prior context.
    if (!resumed_messages.empty()) {
        {
            std::lock_guard<std::mutex> g(state_mtx);
            state.history = resumed_messages;
        }
        std::ostringstream o;
        for (auto& m : resumed_messages) o << render_message(m, theme, ui.width());
        o << theme.dim << "(resumed " << resumed_messages.size()
          << " messages from " << session_path << ")\n\x1b[0m";
        ui.emit(o.str());
    }

    // Compaction (manual /compact and automatic pre-turn). Returns true if a
    // compaction actually happened. `automatic` only changes the wording.
    // Must be called with NO lock held (it takes state_mtx internally).
    auto compaction_settings = [&]() {
        pi::coding::CompactionSettings s;
        if (model.context_window > 0) s.target_context = model.context_window;
        return s;
    };
    auto do_compact = [&](bool automatic) -> bool {
        std::vector<pi::ai::Message> hist_copy;
        {
            std::lock_guard<std::mutex> g(state_mtx);
            hist_copy = state.history;
        }
        if (hist_copy.empty()) {
            if (!automatic) ui.emit(theme.dim + "(no history to compact)\n\x1b[0m");
            return false;
        }
        ui.set_footer(automatic ? "auto-compacting…" : "compacting…");
        auto compact_res = pi::coding::compact(model, opts, hist_copy, compaction_settings());
        bool did = false;
        if (compact_res.aborted) {
            ui.emit(theme.error + "(compaction failed)\n\x1b[0m");
        } else if (compact_res.drop_count == 0) {
            if (!automatic) ui.emit(theme.dim + "(nothing to compact)\n\x1b[0m");
        } else {
            {
                std::lock_guard<std::mutex> g(state_mtx);
                state.history = compact_res.kept_messages;
                state.compaction_count++;
            }
            std::ostringstream o;
            o << theme.dim << "[" << (automatic ? "auto-" : "")
              << "compacted " << compact_res.drop_count << " earlier messages ("
              << state.compaction_count << " total)]\n\x1b[0m";
            ui.emit(o.str());
            if (state.session) {
                pi::coding::SessionEntry e;
                e.type = "compaction";
                e.data["summary"] = compact_res.summary;
                e.data["droppedCount"] = compact_res.drop_count;
                state.session->append_entry(e);
            }
            did = true;
        }
        ui.set_footer("");
        return did;
    };

    // Spawn the agent loop on a detached background thread so the main
    // loop keeps polling keys (INC-002).
    auto spawn_agent = [&](std::vector<pi::ai::Message> messages,
                            pi::agent::AgentLoopConfig cfg_in) {
        auto abort_signal = std::make_shared<pi::agent::MutableAbort>();
        cfg_in.signal = abort_signal;

        {
            std::lock_guard<std::mutex> g(state_mtx);
            state.streaming = true;
            state.current_text.clear();
            state.think = ThinkFilter{};
            state.abort = abort_signal;
        }
        ui.set_footer("thinking…");

        std::thread([&state_mtx, &state, &ui, &theme,
                      messages = std::move(messages), cfg = std::move(cfg_in),
                      append_message_entry_fn = append_message_entry]() mutable {
            auto stream = pi::agent::run_agent_loop(std::move(messages), std::move(cfg));
            stream->drain([&](const pi::agent::AgentEvent& ev) {
                std::string emit_text;   // chat to append (outside state lock)
                std::string footer_msg;  // footer update, if any
                bool have_footer = false;
                int in_tok = -1, out_tok = -1;
                {
                    std::lock_guard<std::mutex> g(state_mtx);
                    switch (ev.kind) {
                        case pi::agent::AgentEvent::Kind::MessageUpdate: {
                            auto& aev = ev.assistant_event;
                            if (aev.kind == pi::ai::AssistantMessageEvent::Kind::TextDelta) {
                                // ThinkFilter splits inline <think>…</think>
                                // tags (MiniMax/DeepSeek). Reasoning regions
                                // get italic + thinkingText, matching upstream.
                                std::string enter = std::string("\n \x1b[3m") + theme.thinking_text;
                                std::string leave = "\x1b[0m\n";
                                std::string before = state.current_text;
                                state.think.feed(aev.delta, enter, leave, state.current_text);
                                // Emit only the newly-appended portion.
                                if (state.current_text.size() > before.size())
                                    emit_text = state.current_text.substr(before.size());
                                footer_msg = state.think.in_think ? "thinking…" : "streaming…";
                                have_footer = true;
                            } else if (aev.kind == pi::ai::AssistantMessageEvent::Kind::ThinkingDelta) {
                                // Dedicated reasoning stream: italic thinkingText.
                                emit_text = "\x1b[3m" + theme.thinking_text + aev.delta + "\x1b[0m";
                                state.current_text += emit_text;
                                footer_msg = "thinking…";
                                have_footer = true;
                            } else if (aev.kind == pi::ai::AssistantMessageEvent::Kind::ToolCallEnd) {
                                emit_text = msg::tool_execution(
                                    aev.tool_call.name, "", msg::ToolState::Pending,
                                    theme, ui.width()) + "\n";
                                state.current_text += emit_text;
                            }
                            break;
                        }
                        case pi::agent::AgentEvent::Kind::MessageEnd: {
                            // INC-006: surface assistant messages (text,
                            // errors, aborts) to the user.
                            if (std::holds_alternative<pi::ai::AssistantMessage>(ev.message)) {
                                const auto& am = std::get<pi::ai::AssistantMessage>(ev.message);
                                std::string final_text;
                                for (auto& c : am.content) {
                                    if (std::holds_alternative<pi::ai::TextContent>(c))
                                        final_text += std::get<pi::ai::TextContent>(c).text;
                                }
                                // Only fall back to MessageEnd's text when the
                                // stream produced nothing (no deltas).
                                if (!final_text.empty() && state.current_text.empty()) {
                                    emit_text += final_text;
                                    state.current_text += final_text;
                                }
                                // Error / abort in red, with a humanized message.
                                if (am.stop_reason == "error" || am.stop_reason == "aborted") {
                                    std::string detail;
                                    if (am.stop_reason == "aborted") {
                                        detail = "aborted";
                                    } else {
                                        detail = pi::ai::humanize_stream_error(
                                            am.provider,
                                            am.error_message ? *am.error_message : "");
                                    }
                                    std::ostringstream o;
                                    o << "\n" << theme.error
                                      << "[" << am.stop_reason << ": " << detail
                                      << "]\x1b[0m\n";
                                    emit_text += o.str();
                                    footer_msg = am.stop_reason;
                                    have_footer = true;
                                }
                                if (am.usage.is_object()) {
                                    in_tok = am.usage.value("inputTokens",
                                                            am.usage.value("input_tokens", 0));
                                    out_tok = am.usage.value("outputTokens",
                                                             am.usage.value("output_tokens", 0));
                                }
                            }
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
                            emit_text = msg::tool_execution(
                                ev.tool_name, preview, msg::ToolState::Pending,
                                theme, ui.width()) + "\n";
                            break;
                        }
                        case pi::agent::AgentEvent::Kind::ToolExecutionEnd: {
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
                            emit_text = msg::tool_execution(
                                ev.tool_name, first,
                                ev.tool_is_error ? msg::ToolState::Error : msg::ToolState::Success,
                                theme, ui.width()) + "\n";
                            break;
                        }
                        case pi::agent::AgentEvent::Kind::AgentEnd:
                            state.history = ev.messages;
                            break;
                        default:
                            break;
                    }
                }
                if (!emit_text.empty()) ui.emit(emit_text);
                if (in_tok >= 0 || out_tok >= 0)
                    ui.set_tokens(in_tok < 0 ? 0 : in_tok, out_tok < 0 ? 0 : out_tok);
                if (have_footer) ui.set_footer(footer_msg);
            });

            // Agent thread done: terminate the turn with a blank separator.
            {
                std::lock_guard<std::mutex> g(state_mtx);
                state.streaming = false;
                state.current_text.clear();
            }
            ui.emit("\n");
            ui.set_footer("");
        }).detach();
    };

    // ----------------------------------------------------------------------
    // Main loop: single-threaded poll of the keyboard. The agent thread
    // streams output concurrently via ui.emit (serialized by Ui's mutex).
    // ----------------------------------------------------------------------
    auto stream_started = std::chrono::steady_clock::now();
    int spin_tick = 0;
    bool last_streaming = false;
    auto last_size = term.size();
    bool quit = false;

    static const char* kFrames[] = {"⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧","⠇","⠏"};

    while (!quit) {
        // Resize detection (SIGWINCH handler is a no-op; we poll size()).
        auto sz = term.size();
        if (sz != last_size) {
            last_size = sz;
            ui.setup();  // rebuild scroll region + redraw bottom
        }

        // Liveness heartbeat while streaming: spinner + elapsed seconds.
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
            if (spin_tick % 4 == 0) {  // ~200ms (4 × 50ms poll)
                int sec = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - stream_started).count();
                std::ostringstream u;
                u << kFrames[(spin_tick / 4) % 10] << " " << sec << "s";
                ui.set_footer(u.str());
            }
            spin_tick++;
        } else {
            last_streaming = false;
            spin_tick = 0;
        }

        // Read keys with a short timeout so we keep polling resize/spinner.
        auto k = term.try_read_key(50);
        if (!k) continue;

        if (k->kind == pi::tui::KeyEvent::Kind::CtrlC) {
            // Two-press: first Ctrl-C aborts the in-flight run; a second
            // within 2s force-quits even if the agent is still winding down.
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
                quit = true;
                break;
            }
            ui.set_footer("aborting (Ctrl-C again to force quit)…");
            continue;
        }

        // Feed the key to the input editor. While streaming we still let the
        // user type into the buffer (it just won't submit until the agent
        // finishes) — don't block their train of thought.
        if (input->on_key(*k)) {
            ui.redraw_bottom();
            if (input->take_submit()) {
                std::string text = input->text();
                input->set_text("");
                input->push_history(text);
                ui.redraw_bottom();
                std::string cmd = std::string(pi::core::str::trim(text));
                if (cmd.empty()) continue;

                // ---- Slash commands ----
                if (cmd == "/exit" || cmd == "/quit") { quit = true; break; }
                if (cmd == "/clear") {
                    ui.setup();  // clears screen + reinstalls scroll region
                    continue;
                }
                if (cmd == "/help") { help_text(); continue; }
                if (cmd == "/new") {
                    {
                        std::lock_guard<std::mutex> g(state_mtx);
                        state.history.clear();
                    }
                    ui.set_tokens(0, 0);
                    ui.emit(theme.dim + "(started new conversation)\n\x1b[0m");
                    continue;
                }
                if (cmd.rfind("/model ", 0) == 0) {
                    ui.emit(theme.dim +
                        "(model switch not yet implemented in V1: " + cmd.substr(7) + ")\x1b[0m\n");
                    continue;
                }
                if (cmd.rfind("/login", 0) == 0) {
                    std::string provider = cmd.size() > 7 ? std::string(pi::core::str::trim(cmd.substr(7))) : "";
                    if (provider.empty()) {
                        ui.emit(theme.error +
                            "Usage: /login <provider>\n"
                            "Known: anthropic (Claude.ai OAuth — V2 framework only)\n"
                            "       For most providers, use --api-key or set API key env var.\n\x1b[0m");
                    } else if (provider != "anthropic") {
                        ui.emit(theme.error +
                            "/login for '" + provider + "' is not yet wired in V2. "
                            "Use --api-key or env var instead.\n\x1b[0m");
                    } else {
                        ui.emit(theme.dim +
                            "(/login anthropic: OAuth framework present, flow not wired in V2)\n\x1b[0m");
                    }
                    continue;
                }
                if (cmd == "/compact") {
                    do_compact(/*automatic=*/false);
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
                    ui.emit(o.str());
                    continue;
                }
                if (cmd == "/sessions") {
                    auto all = coding::SessionManager::list_all();
                    if (all.empty()) {
                        ui.emit(theme.dim + "(no saved sessions)\n\x1b[0m");
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
                        ui.emit(o.str());
                    }
                    continue;
                }
                if (cmd == "/continue" || cmd.rfind("/resume", 0) == 0) {
                    std::string target;
                    if (cmd == "/continue") {
                        auto all = coding::SessionManager::list_all();
                        if (all.empty()) {
                            ui.emit(theme.error + "(no saved sessions)\n\x1b[0m");
                            continue;
                        }
                        target = all.front().path;
                    } else {
                        std::string arg = cmd.size() > 7 ? std::string(pi::core::str::trim(cmd.substr(7))) : "";
                        if (arg.empty()) {
                            ui.emit(theme.error + "Usage: /resume <session-id-prefix>\n\x1b[0m");
                            continue;
                        }
                        target = coding::SessionManager::resolve_id_prefix(arg);
                        if (target.empty()) {
                            ui.emit(theme.error +
                                "(no unique session matches '" + arg + "')\n\x1b[0m");
                            continue;
                        }
                    }
                    coding::SessionManager sm(target);
                    auto hdr = sm.read_header();
                    if (!hdr) {
                        ui.emit(theme.error + "(failed to read header of " + target + ")\n\x1b[0m");
                        continue;
                    }
                    auto entries = sm.read_entries();
                    auto ctx = coding::build_session_context(*hdr, entries, false);
                    {
                        std::lock_guard<std::mutex> g(state_mtx);
                        delete state.session;
                        state.session = new coding::SessionManager(target);
                        state.history = ctx.messages;
                    }
                    ui.setup();  // clear screen for the resumed transcript
                    std::ostringstream o;
                    for (auto& m : ctx.messages) o << render_message(m, theme, ui.width());
                    o << theme.dim << "(resumed " << ctx.messages.size()
                      << " messages from " << target << ")\n\x1b[0m";
                    ui.emit(o.str());
                    continue;
                }
                if (cmd == "/tree") {
                    ui.emit(theme.dim +
                        "[session tree — V2: not yet wired into interactive mode]\n\x1b[0m");
                    continue;
                }

                // Block new submissions while a previous one is running
                // (V1: notify and drop the steering message).
                {
                    bool busy;
                    {
                        std::lock_guard<std::mutex> g(state_mtx);
                        busy = state.streaming;
                    }
                    if (busy) {
                        ui.emit(theme.dim +
                            "(agent still running; your message was discarded. Ctrl-C to cancel.)\n\x1b[0m");
                        continue;
                    }
                }

                // Echo the user prompt into scrollback and push to history.
                {
                    ui.emit(msg::user_message(text, theme, ui.width()) + "\n");
                    std::lock_guard<std::mutex> g(state_mtx);
                    state.history.push_back(pi::ai::UserMessage{});
                    std::get<pi::ai::UserMessage>(state.history.back()).content.push_back(
                        pi::ai::TextContent{text});
                }

                // Auto-compaction: if the (now including this prompt) history
                // is about to overflow the model's context window, summarize
                // the older prefix before sending. Keeps long sessions alive.
                {
                    std::vector<pi::ai::Message> hist_snapshot;
                    {
                        std::lock_guard<std::mutex> g(state_mtx);
                        hist_snapshot = state.history;
                    }
                    if (pi::coding::should_compact(hist_snapshot, compaction_settings()))
                        do_compact(/*automatic=*/true);
                }

                // Build tools and run the agent on a background thread.
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
                cfg.system_prompt = system_prompt;

                spawn_agent(std::move(msgs), std::move(cfg));
            }
            continue;
        }
    }

    ui.teardown();
    term.leave_raw_mode();
    std::cout << "bye.\n";
    return 0;
}

}  // namespace pi::tui::modes
