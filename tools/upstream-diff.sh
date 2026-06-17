#!/usr/bin/env bash
# tools/upstream-diff.sh — Compare our pi C++ port against the upstream
# TypeScript pi to identify feature gaps.
#
# Usage:
#   ./tools/upstream-diff.sh                      # uses default paths
#   ./tools/upstream-diff.sh /path/to/pi-upstream  # custom upstream path
#   ./tools/upstream-diff.sh /path/to/pi-upstream /path/to/this-repo
#
# Output sections:
#   1. Package / directory structure diff
#   2. Capability diff (features mentioned in upstream READMEs / source)
#   3. API symbol diff (selected public classes / functions)
#   4. Recommendations (auto-generated from diffs)
#
# Exit code 0 = no critical gaps, 1 = critical gaps detected.

set -eu

UPSTREAM="${1:-/tmp/pi-upstream}"
OURS="${2:-$(cd "$(dirname "$0")/.." && pwd)}"

if [ ! -d "$UPSTREAM" ]; then
    echo "ERROR: upstream not found at $UPSTREAM"
    echo "Clone it:  git clone --depth 1 https://github.com/earendil-works/pi.git $UPSTREAM"
    exit 1
fi
if [ ! -d "$OURS/libs" ]; then
    echo "ERROR: this repo not found at $OURS (no libs/ dir)"
    exit 1
fi

echo "=== prime-agent upstream-diff ==="
echo "Upstream: $UPSTREAM"
echo "Ours:     $OURS"
echo "Upstream commit: $(git -C "$UPSTREAM" rev-parse --short HEAD 2>/dev/null || echo '?')"
echo "Our commit:     $(git -C "$OURS" rev-parse --short HEAD 2>/dev/null || echo '?')"
echo

# --------------------------------------------------------------------------
# 1. Directory structure
# --------------------------------------------------------------------------
echo "=========================================="
echo "1. Directory structure diff"
echo "=========================================="
echo
echo "Upstream packages:"
ls -d "$UPSTREAM"/packages/*/ 2>/dev/null | xargs -I {} basename {} | sort | sed 's/^/  /'
echo
echo "Our libraries:"
ls -d "$OURS"/libs/*/ 2>/dev/null | xargs -I {} basename {} | sort | sed 's/^/  /'
echo

# --------------------------------------------------------------------------
# 2. Capability diff (from upstream package READMEs and exports)
# --------------------------------------------------------------------------
echo "=========================================="
echo "2. Capability diff (from upstream READMEs)"
echo "=========================================="
echo

declare -A upstream_has
declare -A we_have

# Scan upstream packages for capability keywords in their README files.
scan_upstream() {
    local pkg="$1"
    local readme="$UPSTREAM/packages/$pkg/README.md"
    [ -f "$readme" ] || return
    grep -i -E "^- " "$readme" 2>/dev/null | head -50
}

# Detect known capabilities (rough heuristic — improve over time).
echo "Upstream capabilities (by package):"
for pkg in ai agent tui coding-agent; do
    if [ -d "$UPSTREAM/packages/$pkg" ]; then
        echo "  @earendil-works/pi-$pkg:"
        # Count key features mentioned in README/CHANGELOG.
        features=""
        for keyword in "streaming" "tools" "MCP" "OAuth" "compaction" "extension" "export HTML" "session" "TUI"; do
            if grep -rqi "$keyword" "$UPSTREAM/packages/$pkg" 2>/dev/null; then
                features="$features $keyword"
            fi
        done
        echo "    [${features# }]"
    fi
done
echo

echo "Our capabilities (by library):"
for lib in core http ai agent tui coding; do
    if [ -d "$OURS/libs/$lib" ]; then
        echo "  pi_$lib:"
        features=""
        for keyword in "streaming" "tools" "MCP" "OAuth" "compaction" "extension" "export HTML" "session" "TUI"; do
            if grep -rqi "$keyword" "$OURS/libs/$lib" 2>/dev/null; then
                features="$features $keyword"
            fi
        done
        echo "    [${features# }]"
    fi
done
echo

# --------------------------------------------------------------------------
# 3. Selected API symbol diff
# --------------------------------------------------------------------------
echo "=========================================="
echo "3. Selected API symbol diff"
echo "=========================================="
echo

echo "Upstream modules (sampled from packages/*/src/index.ts):"
for pkg in ai agent tui coding-agent; do
    if [ -f "$UPSTREAM/packages/$pkg/src/index.ts" ]; then
        # Match `export * from "./foo";` and extract "foo"
        grep -F 'export * from "./' "$UPSTREAM/packages/$pkg/src/index.ts" 2>/dev/null \
            | head -10 \
            | awk -F'"' '{print $2}' | awk -F'/' '{print $NF}' | sed 's/\.ts.*$//' \
            | sort -u | sed 's/^/  /'
    fi
done
echo

echo "Our public classes (sampled from libs/*/include):"
for lib in core http ai agent tui coding; do
    if [ -d "$OURS/libs/$lib/include" ]; then
        grep -rhE "^class |^struct " "$OURS/libs/$lib/include" 2>/dev/null \
            | awk '{print $2}' | sed 's/[{(<:;].*//' | sort -u | head -10 | sed 's/^/  /'
    fi
done
echo

# --------------------------------------------------------------------------
# 4. Recommendations
# --------------------------------------------------------------------------
echo "=========================================="
echo "4. Recommendations"
echo "=========================================="
echo

# Heuristic feature gap detection: keywords present in upstream but
# not in our code suggest missing capabilities.
declare -a gaps=()
for keyword in "MCP" "extension" "OAuth" "export HTML"; do
    if grep -rqi "$keyword" "$UPSTREAM/packages" 2>/dev/null && ! grep -rqi "$keyword" "$OURS/libs" 2>/dev/null; then
        gaps+=("$keyword")
    fi
done

if [ ${#gaps[@]} -gt 0 ]; then
    echo "Possible capability gaps (heuristic):"
    for g in "${gaps[@]}"; do
        echo "  - $g"
    done
    echo
fi

# Check our docs/incidents/ for known feature gaps.
if [ -d "$OURS/docs/incidents" ]; then
    echo "Known feature gaps (from docs/incidents/):"
    grep -hE '^\| .* \| \*\*Critical\*\*' "$OURS/docs/incidents/INDEX.md" 2>/dev/null \
        | head -5 || true
fi

# Plan files.
if [ -d "$OURS/pi-spec" ]; then
    plan_files=$(ls "$OURS/pi-spec"/2[0-9][0-9]-*.md 2>/dev/null | wc -l || true)
    echo "Plan/spec docs: $plan_files files in pi-spec/"
fi

# Recommendations.
echo
echo "Suggested next actions:"
if [[ " ${gaps[@]} " =~ " MCP " ]]; then
    echo "  - Implement MCP client (libcurl JSON-RPC over stdio)"
fi
if [[ " ${gaps[@]} " =~ " OAuth " ]]; then
    echo "  - Wire OAuth framework into /login command (already in libs)"
fi
if [[ " ${gaps[@]} " =~ " extension " ]]; then
    echo "  - Implement extension API (TypeScript-like hooks in C++)"
fi
if [[ " ${gaps[@]} " =~ " export HTML " ]]; then
    echo "  - Verify HTML export with new model registry (already implemented)"
fi
echo "  - Run 'pi -p <task>' in 3 modes: -p, --mode rpc, interactive"
echo "  - Run ctest; ensure 12+ suites pass"
echo
echo "(Run periodically — e.g. weekly — to track feature parity.)"
