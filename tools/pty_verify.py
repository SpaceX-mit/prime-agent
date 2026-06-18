#!/usr/bin/env python3
"""PTY verification for prime-agent's interactive TUI rewrite.

Drives the built `pi` binary under a real pseudo-terminal and asserts the
"well-behaved CLI" terminal-control contract:

  * On startup it installs a DECSTBM scroll region  \x1b[1;<rows-2>r
  * It does NOT enter the alternate screen           \x1b[?1049h
  * It does NOT enable mouse reporting                \x1b[?1000h/1003h/1006h
  * On quit (/exit) it resets the scroll region       \x1b[r
  * It re-installs the region after a SIGWINCH resize

No API key required: we never submit a prompt, only exercise the TUI shell
(startup, a keystroke, a resize, and /exit). The agent loop is never run.

Exit code 0 = all checks passed.
"""
import os, sys, pty, time, signal, select, fcntl, termios, struct, re

PI = sys.argv[1] if len(sys.argv) > 1 else "build/apps/pi/pi"
ROWS, COLS = 30, 100

def set_winsize(fd, rows, cols):
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))

def drain(fd, timeout=0.6):
    """Read everything available within `timeout` seconds."""
    buf = b""
    end = time.time() + timeout
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.1)
        if r:
            try:
                chunk = os.read(fd, 65536)
            except OSError:
                break
            if not chunk:
                break
            buf += chunk
            end = time.time() + timeout  # keep reading while data flows
    return buf

def main():
    if not os.path.exists(PI):
        print(f"FAIL: binary not found: {PI}", file=sys.stderr)
        return 2

    pid, fd = pty.fork()
    if pid == 0:  # child
        # No API key on purpose; interactive mode still starts the TUI shell.
        env = dict(os.environ)
        env["ANTHROPIC_API_KEY"] = "pty-dummy-key"
        env["TERM"] = "xterm-256color"
        os.execvpe(PI, [PI], env)
        os._exit(127)

    # parent
    set_winsize(fd, ROWS, COLS)
    captured = b""

    # 1. Startup output.
    captured += drain(fd, 0.8)

    # 2. Type a character (exercise input redraw), then clear it.
    os.write(fd, b"x")
    captured += drain(fd, 0.3)
    os.write(fd, b"\x7f")  # backspace
    captured += drain(fd, 0.3)

    # 3. Resize: shrink then grow, send SIGWINCH each time.
    set_winsize(fd, 20, 80); os.kill(pid, signal.SIGWINCH)
    captured += drain(fd, 0.4)
    set_winsize(fd, ROWS, COLS); os.kill(pid, signal.SIGWINCH)
    captured += drain(fd, 0.4)

    # 4. Submit a normal message → renders the user-message box (userMessageBg
    #    truecolor, full-width padded). The agent loop then errors out fast on
    #    the dummy key, which is fine — we only need the user-box render.
    os.write(fd, "你好 hello\r".encode("utf-8"))
    captured += drain(fd, 1.0)

    # 5. Quit cleanly.
    os.write(fd, b"/exit\r")
    captured += drain(fd, 0.8)

    try:
        os.waitpid(pid, os.WNOHANG)
    except OSError:
        pass
    try:
        os.close(fd)
    except OSError:
        pass

    text = captured.decode("utf-8", "replace")

    def has(pat):
        return re.search(pat, text) is not None

    checks = []
    # DECSTBM installed with bottom = rows-2 = 28 at startup (and again after
    # final resize back to 30 rows).
    checks.append(("DECSTBM region \\x1b[1;28r present",
                   "\x1b[1;28r" in text))
    # After shrink to 20 rows, region should be 1;18.
    checks.append(("DECSTBM region \\x1b[1;18r after resize",
                   "\x1b[1;18r" in text))
    # No alternate screen.
    checks.append(("no alt-screen enter \\x1b[?1049h",
                   "\x1b[?1049h" not in text))
    # No mouse reporting.
    checks.append(("no mouse ?1000h", "\x1b[?1000h" not in text))
    checks.append(("no mouse ?1003h", "\x1b[?1003h" not in text))
    checks.append(("no mouse ?1006h", "\x1b[?1006h" not in text))
    # Scroll region reset on exit.
    checks.append(("scroll region reset \\x1b[r on exit",
                   "\x1b[r" in text))
    # Bracketed paste still enabled (kept).
    checks.append(("bracketed paste ?2004h kept",
                   "\x1b[?2004h" in text))
    # Visual parity: user-message box uses upstream userMessageBg truecolor
    # (#343541 → 48;2;52;53;65).
    checks.append(("user-message bg truecolor 48;2;52;53;65",
                   "\x1b[48;2;52;53;65m" in text))
    # Footer/stats uses dim truecolor fg (#666666 → 38;2;102;102;102).
    checks.append(("dim truecolor 38;2;102;102;102 present",
                   "\x1b[38;2;102;102;102m" in text))

    ok = True
    for name, passed in checks:
        print(f"  [{'PASS' if passed else 'FAIL'}] {name}")
        ok = ok and passed

    if not ok:
        # Dump a sanitized view of the escape traffic for debugging.
        vis = text.replace("\x1b", "<ESC>")
        print("\n--- captured (ESC-expanded, first 1500 chars) ---", file=sys.stderr)
        print(vis[:1500], file=sys.stderr)

    print("\nRESULT:", "PASS" if ok else "FAIL")
    return 0 if ok else 1

if __name__ == "__main__":
    sys.exit(main())
