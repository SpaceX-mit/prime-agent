#!/usr/bin/env bash
# tools/release.sh — Build + package prime-agent into a tarball.
#
# Produces:
#   dist/pi-<version>-<platform>-<arch>.tar.gz
#
# with:
#   bin/pi
#   pi-spec/  (all .md files)
#   README.md

set -euo pipefail

cd "$(dirname "$0")/.."

VERSION="${VERSION:-$(grep -A2 'project(prime-agent' CMakeLists.txt | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)}"
if [ -z "$VERSION" ]; then VERSION="0.1.0"; fi

PLATFORM="${PLATFORM:-$(uname -s | tr '[:upper:]' '[:lower:]')}"
ARCH="${ARCH:-$(uname -m)}"
case "$ARCH" in
    x86_64)  ARCH=x86_64 ;;
    aarch64|arm64) ARCH=aarch64 ;;
    riscv64) ARCH=riscv64 ;;
esac

BUILD_DIR="${BUILD_DIR:-build}"
DIST_DIR="${DIST_DIR:-dist}"
TARBALL_NAME="pi-${VERSION}-${PLATFORM}-${ARCH}"
TARBALL_PATH="$DIST_DIR/${TARBALL_NAME}.tar.gz"

echo "Building prime-agent $VERSION for $PLATFORM/$ARCH..."
cmake -B "$BUILD_DIR" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" -j"$(nproc 2>/dev/null || echo 4)"

if [ ! -x "$BUILD_DIR/apps/pi/pi" ]; then
    echo "build failed: $BUILD_DIR/apps/pi/pi not found"
    exit 1
fi

mkdir -p "$DIST_DIR/$TARBALL_NAME/bin"
mkdir -p "$DIST_DIR/$TARBALL_NAME/pi-spec"

cp "$BUILD_DIR/apps/pi/pi" "$DIST_DIR/$TARBALL_NAME/bin/"
cp pi-spec/*.md "$DIST_DIR/$TARBALL_NAME/pi-spec/"
cp README.md "$DIST_DIR/$TARBALL_NAME/"

# Generate build-info file.
cat > "$DIST_DIR/$TARBALL_NAME/BUILD-INFO" <<EOF
prime-agent $VERSION
Built:    $(date -u +"%Y-%m-%dT%H:%M:%SZ")
Platform: $PLATFORM
Arch:     $ARCH
Compiler: $(g++ --version | head -1)
EOF

# Create tarball.
tar -czf "$TARBALL_PATH" -C "$DIST_DIR" "$TARBALL_NAME"
rm -rf "$DIST_DIR/$TARBALL_NAME"

SIZE=$(du -h "$TARBALL_PATH" | cut -f1)
echo
echo "Built $TARBALL_PATH ($SIZE)"
echo
echo "Install:"
echo "  tar -xzf $TARBALL_PATH -C /opt"
echo "  ln -sf /opt/$TARBALL_NAME/bin/pi /usr/local/bin/pi"
