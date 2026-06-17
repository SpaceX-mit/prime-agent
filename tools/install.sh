#!/usr/bin/env bash
# tools/install.sh — Install the local pi build to a target prefix.
#
# Usage:
#   ./tools/install.sh                    # install to /usr/local/bin (needs sudo)
#   ./tools/install.sh --prefix ~/.local  # install to ~/.local/bin
#   ./tools/install.sh --uninstall

set -euo pipefail

cd "$(dirname "$0")/.."

PREFIX="/usr/local"
BUILD_DIR="${BUILD_DIR:-build}"
BIN="${BUILD_DIR}/apps/pi/pi"
DOC_DIR_REL="pi-spec"

uninstall=0
for arg in "$@"; do
    case "$arg" in
        --prefix=*) PREFIX="${arg#*=}" ;;
        --prefix)   PREFIX="${2:-}"; shift ;;
        --uninstall) uninstall=1 ;;
        --build-dir=*) BUILD_DIR="${arg#*=}" ;;
        --build-dir)   BUILD_DIR="${2:-}"; shift ;;
        -h|--help)
            cat <<EOF
Usage: $0 [--prefix PATH] [--build-dir PATH] [--uninstall]

Options:
  --prefix PATH    Install root (default: /usr/local)
  --build-dir PATH CMake build directory (default: build)
  --uninstall      Remove installed files
EOF
            exit 0 ;;
    esac
done

BIN_DEST="$PREFIX/bin/pi"
DOC_DEST="$PREFIX/share/prime-agent/$DOC_DIR_REL"

if [ "$uninstall" = "1" ]; then
    echo "Uninstalling from $PREFIX..."
    rm -f "$BIN_DEST"
    rm -rf "$DOC_DEST"
    echo "done."
    exit 0
fi

if [ ! -x "$BIN" ]; then
    echo "build not found: $BIN"
    echo "run 'cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j' first"
    exit 1
fi

echo "Installing to $PREFIX..."
install -d "$PREFIX/bin"
install -d "$DOC_DEST"
install -m 0755 "$BIN" "$BIN_DEST"
if [ -d "$DOC_DIR_REL" ]; then
    cp -r "$DOC_DIR_REL"/* "$DOC_DEST"/
fi

echo
echo "Installed:"
echo "  $BIN_DEST"
echo "  $DOC_DEST ($(ls "$DOC_DEST" 2>/dev/null | wc -l) files)"
echo
echo "Run 'pi --help' to get started."
echo
echo "Set an API key first:"
echo "  export ANTHROPIC_API_KEY=sk-ant-..."
echo "  export OPENAI_API_KEY=sk-..."
echo
echo "Or put it in ~/.pi/agent/auth.json."
