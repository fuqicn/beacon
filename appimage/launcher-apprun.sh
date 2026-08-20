#!/bin/sh
# Beacon launcher AppImage AppRun (POSIX sh)
#
# The launcher AppImage carries the payload beacon-app.AppImage plus
# mirrors.json and version.txt. On first run (or when the version changes) it
# installs those files into <launcher dir>/beacon, then launches the payload
# AppImage through the AppImage runtime so its bundled dependencies resolve
# correctly.
set -e

# Directory that contains this launcher AppImage.
LAUNCHER_DIR=$(CDPATH= cd -- "$(dirname -- "${APPIMAGE:-$0}")" && pwd)
INSTALL_DIR="$LAUNCHER_DIR/beacon"

# The payload lives in this AppImage's mount; fall back to this script's dir
# when the AppRun was invoked straight from an unpacked AppDir.
if [ -n "${APPDIR:-}" ]; then
    PAYLOAD_DIR="$APPDIR"
else
    PAYLOAD_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
fi
PAYLOAD="$PAYLOAD_DIR/beacon-app.AppImage"

expected=""
if [ -r "$PAYLOAD_DIR/version.txt" ]; then
    expected=$(cat "$PAYLOAD_DIR/version.txt")
fi
current=""
if [ -r "$INSTALL_DIR/version.txt" ]; then
    current=$(cat "$INSTALL_DIR/version.txt")
fi

needs_update=0
if [ "$current" != "$expected" ]; then
    needs_update=1
fi
if [ ! -f "$INSTALL_DIR/beacon-app.AppImage" ]; then
    needs_update=1
fi

if [ "$needs_update" = "1" ]; then
    if ! mkdir -p "$INSTALL_DIR"; then
        echo "Cannot create install directory: $INSTALL_DIR" >&2
        exit 1
    fi
    if ! cp "$PAYLOAD" "$INSTALL_DIR/beacon-app.AppImage"; then
        echo "Failed to install beacon-app.AppImage." >&2
        exit 1
    fi
    if [ -f "$PAYLOAD_DIR/mirrors.json" ]; then
        cp "$PAYLOAD_DIR/mirrors.json" "$INSTALL_DIR/mirrors.json"
    fi
    printf '%s\n' "$expected" > "$INSTALL_DIR/version.txt"
fi

exec "$INSTALL_DIR/beacon-app.AppImage" "$@"