// libs/coding/src/tools/bash.cpp
#include "pi_coding/tools/bash_tool.hpp"

#include "pi_core/file_io.hpp"
#include "pi_core/log.hpp"
#include "pi_core/path.hpp"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace pi::coding::tools {

namespace {

struct Pipe {
    int fd = -1;
    Pipe() = default;
    ~Pipe() { if (fd >= 0) ::close(fd); }
    Pipe(Pipe&& o) noexcept : fd(o.fd) { o.fd = -1; }
    Pipe& operator=(Pipe&& o) noexcept {
        if (this != &o) { if (fd >= 0) ::close(fd); fd = o.fd; o.fd = -1; }
        return *this;
    }
};

bool make_pipe(int (&fds)[2]) {
    if (::pipe(fds) != 0) return false;
    // Set non-blocking on read end.
    int fl = fcntl(fds[0], F_GETFL, 0);
    if (fl >= 0) fcntl(fds[0], F_SETFL, fl | O_NONBLOCK);
    return true;
}

std::string truncate_output(const std::string& s, size_t max) {
    if (s.size() <= max) return s;
    std::ostringstream o;
    o << s.substr(0, max);
    o << "\n... [truncated, " << (s.size() - max) << " more bytes]\n";
    return o.str();
}

}  // namespace

BashTool::BashTool() = default;

pi::core::Json BashTool::parameters() const {
    return pi::core::Json{
        {"type", "object"},
        {"properties", {
            {"command", {{"type", "string"}, {"description", "Shell command to execute"}}},
            {"timeout", {{"type", "number"}, {"description", "Timeout in ms (default 120000)"}}}
        }},
        {"required", {"command"}}
    };
}

pi::agent::ToolResult BashTool::execute(
    const pi::core::Json& args,
    pi::agent::AbortSignal& signal,
    pi::agent::ProgressFn /*on_update*/) {

    pi::agent::ToolResult r;
    if (!args.contains("command") || !args["command"].is_string()) {
        pi::ai::TextContent t; t.text = "Error: missing required argument 'command'";
        r.content = {t};
        r.is_error = true;
        return r;
    }

    std::string cmd = args["command"].get<std::string>();
    int timeout_ms = args.value("timeout", 120000);

    int out_fds[2] = {-1, -1};
    if (!make_pipe(out_fds)) {
        pi::ai::TextContent t; t.text = "Error: pipe() failed";
        r.content = {t};
        r.is_error = true;
        return r;
    }

    pid_t pid = ::fork();
    if (pid < 0) {
        ::close(out_fds[0]); ::close(out_fds[1]);
        pi::ai::TextContent t; t.text = std::string("Error: fork failed: ") + std::strerror(errno);
        r.content = {t};
        r.is_error = true;
        return r;
    }

    if (pid == 0) {
        // Child: redirect stdout+stderr to pipe, exec shell.
        ::dup2(out_fds[1], STDOUT_FILENO);
        ::dup2(out_fds[1], STDERR_FILENO);
        ::close(out_fds[0]);
        ::close(out_fds[1]);
        // Restore default signal handlers in case parent set handlers for abort.
        ::signal(SIGINT, SIG_DFL);
        ::signal(SIGTERM, SIG_DFL);
        ::signal(SIGPIPE, SIG_DFL);
        const char* shell = std::getenv("SHELL");
        if (!shell || !*shell) shell = "/bin/sh";
        const char* argv[] = {shell, "-c", cmd.c_str(), nullptr};
        ::execvp(shell, const_cast<char* const*>(argv));
        ::_exit(127);
    }

    // Parent.
    ::close(out_fds[1]);

    std::string output;
    char buf[4096];
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    bool killed = false;
    while (true) {
        if (signal.aborted()) {
            ::kill(pid, SIGTERM);
            killed = true;
            break;
        }
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            ::kill(pid, SIGTERM);
            killed = true;
            break;
        }
        // poll
        int remaining = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
        struct pollfd pfd{out_fds[0], POLLIN, 0};
        int pr = ::poll(&pfd, 1, std::min(remaining, 200));
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (pr == 0) continue;  // tick — recheck time / signal
        ssize_t n = ::read(out_fds[0], buf, sizeof(buf));
        if (n > 0) output.append(buf, static_cast<size_t>(n));
        else if (n == 0) break;  // EOF: child closed pipe
        else if (errno != EINTR) break;
    }

    // Reap the child (non-blocking).
    int status = 0;
    bool exited = false;
    int retry = 0;
    while (retry++ < 100) {
        pid_t wp = ::waitpid(pid, &status, WNOHANG);
        if (wp == pid) { exited = true; break; }
        if (wp < 0) {
            if (errno == EINTR) continue;
            break;
        }
        // Not yet exited; SIGKILL if needed, else wait briefly and retry.
        if (killed) {
            ::kill(pid, SIGKILL);
        }
        struct timespec ts{0, 10 * 1000 * 1000};  // 10ms
        ::nanosleep(&ts, nullptr);
    }
    ::close(out_fds[0]);

    // Format the output.
    output = truncate_output(output, kMaxOutputBytes);
    int exit_code = exited ? WEXITSTATUS(status) : -1;
    int signal_no = exited ? WIFSIGNALED(status) ? WTERMSIG(status) : 0 : 0;

    pi::core::Json details = {
        {"exitCode", exit_code},
        {"signal", signal_no},
        {"killed", killed},
        {"outputBytes", output.size()},
    };

    pi::ai::TextContent t;
    if (killed) {
        t.text = output + "\n[killed by signal/timeout]";
    } else {
        t.text = output;
        if (exit_code != 0) {
            t.text += "\n[exit " + std::to_string(exit_code) + "]";
        }
    }
    r.content = {t};
    r.details = details;
    r.is_error = (exit_code != 0) || killed || signal.aborted();
    return r;
}

}  // namespace pi::coding::tools
