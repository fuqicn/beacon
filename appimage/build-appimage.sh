#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build-x86_64"
APPIMAGE_DIR="$PROJECT_ROOT/build-appimage"
APPDIR="$APPIMAGE_DIR/AppDir"
LINUXDEPLOY="$APPIMAGE_DIR/linuxdeploy"
QT_DIR="/home/fuqi/Qt/6.11.1/gcc_64"

BUILD_TYPE="Release"
if [[ "${1:-}" == "--debug" ]]; then
    BUILD_TYPE="Debug"
fi

echo "=== Building Beacon AppImage ($BUILD_TYPE) ==="

# Step 1: Download linuxdeploy
if [[ ! -f "$LINUXDEPLOY" ]]; then
    echo "--- Downloading linuxdeploy ---"
    curl -L -o "$APPIMAGE_DIR/linuxdeploy" \
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
    chmod +x "$APPIMAGE_DIR/linuxdeploy"
fi

if [[ ! -f "$APPIMAGE_DIR/linuxdeploy-plugin-qt" ]]; then
    echo "--- Downloading linuxdeploy Qt plugin ---"
    curl -L -o "$APPIMAGE_DIR/linuxdeploy-plugin-qt" \
        "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage"
    chmod +x "$APPIMAGE_DIR/linuxdeploy-plugin-qt"
fi

# Step 2: CMake build (use build-x86_64 to avoid qt_add_qml_module conflict)
echo "--- Configuring CMake ---"
cmake -B "$BUILD_DIR" -S "$PROJECT_ROOT" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_PREFIX_PATH="$QT_DIR" \
    -G Ninja

echo "--- Building ---"
cmake --build "$BUILD_DIR" --target Beacon -j"$(nproc)"

# Step 3: Install into AppDir
echo "--- Installing into AppDir ---"
rm -rf "$APPDIR"
mkdir -p "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD_DIR"

# Step 4: Copy desktop file and icon
mkdir -p "$APPDIR/usr/share/applications"
mkdir -p "$APPDIR/usr/share/icons/hicolor/scalable/apps"
cp "$SCRIPT_DIR/beacon.desktop" "$APPDIR/usr/share/applications/io.github.fuqicn.beacon.desktop"
cp "$PROJECT_ROOT/Untitled.svg" "$APPDIR/usr/share/icons/hicolor/scalable/apps/io.github.fuqicn.beacon.svg"
ln -sf usr/bin/Beacon "$APPDIR/Beacon"
ln -sf usr/share/applications/io.github.fuqicn.beacon.desktop "$APPDIR/io.github.fuqicn.beacon.desktop"
ln -sf usr/share/icons/hicolor/scalable/apps/io.github.fuqicn.beacon.svg "$APPDIR/io.github.fuqicn.beacon.svg"

# Step 5: linuxdeploy
# STRIP=true works around bundled strip not understanding GCC 16 / Fedora 44 ELF
echo "--- Running linuxdeploy ---"
cd "$APPIMAGE_DIR"

STRIP=true QMAKE="$QT_DIR/bin/qmake" "$LINUXDEPLOY" \
    --appdir "$APPDIR" \
    --plugin qt \
    --output appimage

echo "=== Done: $APPIMAGE_DIR/Beacon-x86_64.AppImage ==="
