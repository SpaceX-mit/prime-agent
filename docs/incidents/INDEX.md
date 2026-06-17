# Incident & Bug-Fix Records

This directory tracks real bugs found in `prime-agent`, written as postmortems.

Each document follows the same structure:

1. **TL;DR** — one-paragraph summary, severity, status
2. **Timeline** — when discovered, when fixed, when verified
3. **Reproduction** — minimal command to reproduce
4. **Root cause** — technical explanation
5. **Fix** — the patch (with file paths + diff summary)
6. **Verification** — before/after evidence
7. **Impact assessment** — who/what was affected
8. **Lessons learned** — how to prevent this class of bug going forward

## Index

| Date | ID | Title | Severity | Status |
|---|---|---|---|---|
| 2026-06-17 | INC-001 | Streaming events silently dropped due to non-blocking `try_pull()` race | **Critical** | ✅ Fixed (commit `67cd7d8`) |
| 2026-06-17 | INC-002 | Interactive mode freezes the main loop during agent runs | **Critical** | ✅ Fixed (commit `ab89f4c`) |
| 2026-06-17 | INC-003 | Multi-turn conversation support missing — each turn loses prior history | **Critical** | ✅ Fixed |

## Severity levels

| Level | Definition |
|---|---|
| Critical | Loss of correctness or visible functionality for end users |
| High | Degraded UX or significant deviation from spec |
| Medium | Minor regression; workaround exists |
| Low | Cosmetic or edge-case |

## How to add a new incident

1. Create `docs/incidents/YYYY-MM-DD-short-slug.md` (use today's date)
2. Use the template in `_TEMPLATE.md` (see git history once added)
3. Add a row to the index table above
4. Reference the offending commit hash once fixed
