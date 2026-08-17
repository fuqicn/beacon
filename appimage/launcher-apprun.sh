#!/bin/sh
# Beacon launcher AppImage AppRun (POSIX sh)
#
# The launcher AppImage carries a single payload file, beacon-app.AppImage,
# which self-extracts the real app into <launcher dir>/beacon and launches it.
#   - if the installed copy is missing or an older version, beacon-app.AppImage
#     is run in update mode (BEACON_UPDATER=1)
#   - then the app is launched through beacon-app.AppImage in RUN mode
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

needs_extract=0
if [ "$current" != "$expected" ]; then
    needs_extract=1
fi
if [ ! -x "$INSTALL_DIR/usr/bin/Beacon" ]; then
    needs_extract=1
fi

if [ "$needs_extract" = "1" ]; then
    if ! BEACON_UPDATER=1 "$PAYLOAD" "$INSTALL_DIR"; then
        echo "Beacon update failed." >&2
        exit 1
    fi
    if [ ! -x "$INSTALL_DIR/usr/bin/Beacon" ]; then
        echo "Beacon binary missing after update." >&2
        exit 1
    fi
fi

exec "$PAYLOAD" "$INSTALL_DIR"