#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build-flatpak"
REPO_DIR="$BUILD_DIR/repo"
MANIFEST="$SCRIPT_DIR/io.github.fuqicn.beacon.yml"

BUILD_REPO=false
if [[ "${1:-}" == "--repo" ]]; then
    BUILD_REPO=true
fi

echo "=== Building Beacon Flatpak ==="
rm -rf "$BUILD_DIR"

if $BUILD_REPO; then
    flatpak-builder \
        --force-clean \
        --repo="$REPO_DIR" \
        --ccache \
        "$BUILD_DIR/app" \
        "$MANIFEST"

    mkdir -p "$PROJECT_ROOT/build-dist"
    flatpak build-bundle \
        "$REPO_DIR" \
        "$PROJECT_ROOT/build-dist/Beacon.flatpak" \
        io.github.fuqicn.beacon

    echo "=== Done: build-dist/Beacon.flatpak ==="
    echo "Install: flatpak install --user build-dist/Beacon.flatpak"
else
    flatpak-builder \
        --force-clean \
        --ccache \
        "$BUILD_DIR/app" \
        "$MANIFEST"

    echo "=== Done ==="
    echo "Install: flatpak-builder --user --install --force-clean build-flatpak/app $MANIFEST"
fi
