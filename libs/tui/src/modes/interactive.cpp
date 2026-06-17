// libs/tui/src/modes/interactive.cpp
// Interactive mode: a minimal but functional REPL.

#include "pi_tui/modes/interactive.hpp"

#include "pi_agent/agent_loop.hpp"
#include "pi_ai/stream_simple.hpp"
#include "pi_coding/compaction.hpp"
#include "pi_coding/session_manager.hpp"
#include "pi_core/ansi.hpp"
#include "pi_core/log.hpp"
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
#include <sstream>
#include <thread>

namespace pi::tui::modes {

namespace {

struct ChatState {
    std::vector<std::string> turns;  // each turn is text+markdown lines
    std::string current_text;
    bool streaming = false;
    std::string status;
    std::vector<pi::ai::Message> history;     // full conversation (for /compact, /tree)
    pi::coding::SessionManager* session = nullptr;
    std::string model_id;
    int compaction_count = 0;
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

    // Build the component tree:
    //   Box (vertical)
    //     <chat scrollback>
    //     <input>
    //     <footer>
    auto input = std::make_shared<components::Input>(theme);
    input->set_prompt(theme.accent + "› " + theme.primary);
    auto footer = std::make_shared<components::Footer>(theme, "interactive", model.id);
    auto chat_box = std::make_shared<components::Box>(components::Box::Vertical, 0);
    (void)chat_box;

    // Use a closure to render the chat as a single Text component that updates.
    auto chat_text = std::make_shared<components::Text>("", theme.primary);

    auto root = std::make_shared<components::Box>(components::Box::Vertical, 0);
    root->add(chat_text);
    root->add(input);
    root->add(footer);
    tui.set_root(root);

    ChatState state;
    auto refresh_chat = [&]() {
        auto lines = render_chat(state, theme, 80);
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
        state.turns.push_back(o.str());
    };

    show_welcome();
    refresh_chat();
    tui.render();

    // Helpers for slash commands.
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
        state.turns.push_back(o.str());
    };

    while (!tui.should_quit()) {
        // Redraw.
        refresh_chat();
        tui.render();

        // Read key.
        auto k = term.try_read_key(100);
        if (!k) continue;

        if (k->kind == pi::tui::KeyEvent::Kind::CtrlC) {
            tui.quit();
            break;
        }

        if (input->on_key(*k)) {
            if (input->take_submit()) {
                std::string text = input->text();
                input->set_text("");
                input->push_history(text);
                // Strip leading prompt whitespace.
                std::string cmd(text);
                cmd = std::string(pi::core::str::trim(cmd));
                if (cmd.empty()) continue;

                if (cmd == "/exit" || cmd == "/quit") {
                    tui.quit();
                    break;
                }
                if (cmd == "/clear") {
                    state.turns.clear();
                    state.current_text.clear();
                    continue;
                }
                if (cmd == "/help") {
                    help_text();
                    continue;
                }
                if (cmd == "/new") {
                    state.turns.clear();
                    state.current_text.clear();
                    std::ostringstream o;
                    o << theme.dim << "(started new conversation)\n\x1b[0m";
                    state.turns.push_back(o.str());
                    continue;
                }
                if (cmd.rfind("/model ", 0) == 0) {
                    state.turns.push_back(theme.dim + "(model switch not yet implemented in V1: " + cmd.substr(7) + ")\x1b[0m\n");
                    continue;
                }
                if (cmd == "/compact") {
                    if (state.history.empty()) {
                        state.turns.push_back(theme.dim + "(no history to compact)\n\x1b[0m");
                        continue;
                    }
                    state.streaming = true;
                    state.status = "compacting…";
                    footer->set_status("compacting…");
                    refresh_chat();
                    tui.render();

                    pi::coding::CompactionSettings settings;
                    auto compact_res = pi::coding::compact(
                        model, opts, state.history, settings);
                    if (compact_res.aborted) {
                        state.turns.push_back(theme.error + "(compaction failed)\n\x1b[0m");
                    } else if (compact_res.drop_count == 0) {
                        state.turns.push_back(theme.dim + "(nothing to compact)\n\x1b[0m");
                    } else {
                        // Replace history with the kept+summary form.
                        state.history = compact_res.kept_messages;
                        state.compaction_count++;
                        std::ostringstream o;
                        o << theme.dim << "[compacted " << compact_res.drop_count
                          << " earlier messages (" << state.compaction_count
                          << " compactions total)]\n\x1b[0m";
                        state.turns.push_back(o.str());

                        // Persist compaction entry if we have a session.
                        if (state.session) {
                            pi::coding::SessionEntry e;
                            e.type = "compaction";
                            e.data["summary"] = compact_res.summary;
                            e.data["droppedCount"] = compact_res.drop_count;
                            state.session->append_entry(e);
                        }
                    }
                    state.streaming = false;
                    state.status.clear();
                    footer->set_status("");
                    refresh_chat();
                    tui.render();
                    continue;
                }
                if (cmd == "/tree") {
                    state.turns.push_back(theme.dim + "[session tree — V2: not yet wired into interactive mode]\n\x1b[0m");
                    continue;
                }

                // Regular prompt: send to agent.
                {
                    std::ostringstream o;
                    o << theme.accent << "› " << theme.primary << text << "\x1b[0m\n";
                    state.turns.push_back(o.str());
                }

                // Build tools.
                std::vector<pi::agent::ToolPtr> tools;
                tools.push_back(std::make_shared<pi::coding::tools::BashTool>());
                tools.push_back(std::make_shared<pi::coding::tools::ReadTool>(cwd));
                tools.push_back(std::make_shared<pi::coding::tools::WriteTool>(cwd));
                tools.push_back(std::make_shared<pi::coding::tools::EditTool>(cwd));

                std::vector<pi::ai::Message> messages;
                pi::ai::UserMessage um;
                um.content.push_back(pi::ai::TextContent{text});
                messages.push_back(um);

                pi::agent::AgentLoopConfig cfg;
                cfg.model = model;
                cfg.tools = std::move(tools);
                cfg.stream_opts = opts;

                state.streaming = true;
                state.current_text.clear();
                state.status = "thinking…";
                footer->set_status("thinking…");
                refresh_chat();
                tui.render();

                auto stream = pi::agent::run_agent_loop(std::move(messages), std::move(cfg));
                stream->drain([&](const pi::agent::AgentEvent& ev) {
                    switch (ev.kind) {
                        case pi::agent::AgentEvent::Kind::MessageUpdate: {
                            auto& aev = ev.assistant_event;
                            if (aev.kind == pi::ai::AssistantMessageEvent::Kind::TextDelta) {
                                state.current_text += aev.delta;
                                state.status = "streaming…";
                                footer->set_status("streaming…");
                                refresh_chat();
                                tui.render();
                            } else if (aev.kind == pi::ai::AssistantMessageEvent::Kind::ToolCallEnd) {
                                std::ostringstream o;
                                o << "\n" << theme.dim << "[tool: " << aev.tool_call.name << "]\x1b[0m\n";
                                state.current_text += o.str();
                                refresh_chat();
                                tui.render();
                            } else if (aev.kind == pi::ai::AssistantMessageEvent::Kind::Done) {
                                state.status = "done";
                                footer->set_status("done");
                            }
                            break;
                        }
                        case pi::agent::AgentEvent::Kind::ToolExecutionEnd: {
                            std::ostringstream o;
                            o << theme.dim << "← " << ev.tool_name
                              << (ev.tool_is_error ? " [error]\n" : " [ok]\n") << "\x1b[0m";
                            state.current_text += o.str();
                            refresh_chat();
                            tui.render();
                            break;
                        }
                        case pi::agent::AgentEvent::Kind::MessageEnd:
                            // Could finalize, but we already accumulated text into current_text.
                            break;
                        default:
                            break;
                    }
                });

                state.streaming = false;
                state.turns.push_back(state.current_text);
                state.current_text.clear();
                state.turns.push_back("");  // blank line
                state.status.clear();
                footer->set_status("");
                refresh_chat();
                tui.render();
            }
            continue;
        }
    }

    term.leave_raw_mode();
    std::cout << "bye.\n";
    return 0;
}

}  // namespace pi::tui::modes
