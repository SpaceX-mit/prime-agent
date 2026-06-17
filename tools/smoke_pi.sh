#!/usr/bin/env bash
# tools/smoke_pi.sh — Smoke test for the local pi build.
#
# Verifies:
#   1. --version works
#   2. --help works
#   3. --list-models returns valid JSON
#   4. Single-turn prompt works against a real provider (env var required)
#
# Usage:
#   ./tools/smoke_pi.sh              # 1-3 only
#   ANTHROPIC_API_KEY=sk-ant-... ./tools/smoke_pi.sh   # also runs 4

set -uo pipefail

cd "$(dirname "$0")/.."

BUILD_DIR="${BUILD_DIR:-build}"
BIN="$BUILD_DIR/apps/pi/pi"

if [ ! -x "$BIN" ]; then
    echo "build not found: $BIN (run 'cmake -B build && cmake --build build -j' first)"
    exit 1
fi

failed=0
pass() { echo "  ✓ $1"; }
fail() { echo "  ✗ $1"; failed=1; }

echo "[1/4] --version"
out=$("$BIN" --version 2>&1) || true
echo "    $out"
echo "$out" | grep -qE '^pi [0-9]' && pass "version output" || fail "version output"

echo "[2/4] --help"
out=$("$BIN" --help 2>&1) || true
echo "    (head)" ; echo "$out" | head -3 | sed 's/^/    /'
echo "$out" | grep -q 'USAGE' && pass "help shows USAGE" || fail "help missing USAGE"

echo "[3/4] --list-models"
out=$("$BIN" --list-models 2>&1) || true
count=$(echo "$out" | grep -c '"id"' || true)
if [ "$count" -ge 5 ]; then
    pass "list-models returned $count models"
else
    fail "list-models expected >= 5, got $count"
fi

echo "[4/4] live prompt"
if [ -z "${ANTHROPIC_API_KEY:-}" ] && [ -z "${OPENAI_API_KEY:-}" ]; then
    echo "    skipped (set ANTHROPIC_API_KEY or OPENAI_API_KEY to enable)"
else
    out=$("$BIN" -p "say exactly: smoke_ok" 2>&1) || rc=$?
    rc=${rc:-0}
    if [ "$rc" -eq 0 ] && echo "$out" | grep -q 'smoke_ok'; then
        pass "live prompt returned expected text"
    else
        echo "    output: $out"
        fail "live prompt (rc=$rc)"
    fi
fi

echo
if [ $failed -eq 0 ]; then
    echo "ALL SMOKE TESTS PASSED"
    exit 0
else
    echo "SMOKE TESTS FAILED"
    exit 1
fi
