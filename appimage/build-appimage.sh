#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build-x86_64"
WORK="$PROJECT_ROOT/build-appimage"
APP_APPDIR="$WORK/app.AppDir"        # payload AppDir  -> beacon-app.AppImage
LAUNCH_APPDIR="$WORK/launch.AppDir"  # launcher AppDir -> Beacon.AppImage
LINUXDEPLOY="$WORK/linuxdeploy"
QT_DIR="/home/fuqi/Qt/6.11.1/gcc_64"
VERSION="${VERSION:-1.0.0}"

BUILD_TYPE="Release"
if [[ "${1:-}" == "--debug" ]]; then
    BUILD_TYPE="Debug"
fi

# AppImage tools self-extract instead of mounting when FUSE is unavailable
# (e.g. inside containers/CI).
export APPIMAGE_EXTRACT_AND_RUN=1
export ARCH=x86_64

echo "=== Building Beacon AppImages ($BUILD_TYPE, version $VERSION) ==="

# Step 1: download tools
mkdir -p "$WORK"
if [[ ! -x "$LINUXDEPLOY" ]]; then
    echo "--- Downloading linuxdeploy ---"
    curl -L -o "$WORK/linuxdeploy" \
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
    chmod +x "$WORK/linuxdeploy"
fi

if [[ ! -x "$WORK/linuxdeploy-plugin-qt" ]]; then
    echo "--- Downloading linuxdeploy Qt plugin ---"
    curl -L -o "$WORK/linuxdeploy-plugin-qt" \
        "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage"
    chmod +x "$WORK/linuxdeploy-plugin-qt"
fi

if [[ ! -x "$WORK/appimagetool" ]]; then
    echo "--- Downloading appimagetool ---"
    curl -L -o "$WORK/appimagetool" \
        "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage"
    chmod +x "$WORK/appimagetool"
fi

# Step 2: CMake build (build-x86_64 avoids qt_add_qml_module conflict)
echo "--- Configuring CMake ---"
cmake -B "$BUILD_DIR" -S "$PROJECT_ROOT" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_PREFIX_PATH="$QT_DIR" \
    -G Ninja

echo "--- Building ---"
cmake --build "$BUILD_DIR" --target Beacon -j"$(nproc)"

# Step 3: install into the app AppDir
echo "--- Installing into app AppDir ---"
rm -rf "$APP_APPDIR"
mkdir -p "$APP_APPDIR"
DESTDIR="$APP_APPDIR" cmake --install "$BUILD_DIR"

# Step 4: desktop file, icon, mirrors.json, version.txt
mkdir -p "$APP_APPDIR/usr/share/applications"
mkdir -p "$APP_APPDIR/usr/share/icons/hicolor/scalable/apps"
cp "$SCRIPT_DIR/beacon.desktop" "$APP_APPDIR/usr/share/applications/io.github.fuqicn.beacon.desktop"
cp "$PROJECT_ROOT/Untitled.svg" "$APP_APPDIR/usr/share/icons/hicolor/scalable/apps/io.github.fuqicn.beacon.svg"
ln -sf usr/bin/Beacon "$APP_APPDIR/Beacon"
ln -sf usr/share/applications/io.github.fuqicn.beacon.desktop "$APP_APPDIR/io.github.fuqicn.beacon.desktop"
ln -sf usr/share/icons/hicolor/scalable/apps/io.github.fuqicn.beacon.svg "$APP_APPDIR/io.github.fuqicn.beacon.svg"

# main.cpp loads mirrors.json from applicationDirPath() == usr/bin on Linux
cp "$PROJECT_ROOT/third_party/minecraft-launcher-kernel/mirrors.json" "$APP_APPDIR/usr/bin/mirrors.json"
printf '%s\n' "$VERSION" > "$APP_APPDIR/version.txt"

# Step 5: bundle Qt with linuxdeploy (bundle only; our own AppRun goes in next)
echo "--- Running linuxdeploy (bundle Qt into app AppDir) ---"
cd "$WORK"
STRIP=true QMAKE="$QT_DIR/bin/qmake" "$LINUXDEPLOY" \
    --appdir "$APP_APPDIR" \
    --plugin qt

# Step 6: compile the C AppRun for the self-extracting AppImage
echo "--- Compiling C AppRun ---"
gcc -O2 -Wall -o "$APP_APPDIR/AppRun" "$SCRIPT_DIR/selfextract.c" \
    $(pkg-config --cflags --libs gtk+-3.0)
chmod +x "$APP_APPDIR/AppRun"

# Step 7: build beacon-app.AppImage (the self-extracting payload)
echo "--- Building beacon-app.AppImage ---"
"$WORK/appimagetool" -v "$VERSION" "$APP_APPDIR" "$WORK/beacon-app.AppImage"
chmod +x "$WORK/beacon-app.AppImage"

# Step 8: assemble the launcher AppDir (single payload file)
echo "--- Assembling launcher AppDir ---"
rm -rf "$LAUNCH_APPDIR"
mkdir -p "$LAUNCH_APPDIR"
cp "$SCRIPT_DIR/launcher-apprun.sh" "$LAUNCH_APPDIR/AppRun"
chmod +x "$LAUNCH_APPDIR/AppRun"
cp "$WORK/beacon-app.AppImage" "$LAUNCH_APPDIR/beacon-app.AppImage"
printf '%s\n' "$VERSION" > "$LAUNCH_APPDIR/version.txt"
cp "$APP_APPDIR/usr/share/applications/io.github.fuqicn.beacon.desktop" "$LAUNCH_APPDIR/io.github.fuqicn.beacon.desktop"
cp "$APP_APPDIR/usr/share/icons/hicolor/scalable/apps/io.github.fuqicn.beacon.svg" "$LAUNCH_APPDIR/io.github.fuqicn.beacon.svg"
sed -i 's/^Exec=Beacon$/Exec=Beacon.AppImage/' "$LAUNCH_APPDIR/io.github.fuqicn.beacon.desktop"

# Step 9: build the launcher AppImage
echo "--- Building Beacon.AppImage ---"
"$WORK/appimagetool" -v "$VERSION" "$LAUNCH_APPDIR" "$WORK/Beacon.AppImage"
chmod +x "$WORK/Beacon.AppImage"

echo "=== Done ==="
echo "  $WORK/beacon-app.AppImage  (self-extracting app payload)"
echo "  $WORK/Beacon.AppImage      (launcher, installs to <its dir>/beacon)"