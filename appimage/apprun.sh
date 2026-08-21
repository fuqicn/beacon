#!/bin/sh
# Beacon payload AppImage AppRun (POSIX sh)
#
# Sets up the Qt runtime environment relative to the AppImage mount ($APPDIR)
# and launches the bundled binary. Used by beacon-app.AppImage so the app runs
# with its own Qt libraries/plugins/QML instead of relying on system ones.

if [ -n "${APPDIR:-}" ]; then
    APP_ROOT="$APPDIR"
else
    APP_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
fi

prepend_path() {
    var=$1
    dir=$2
    current=$(eval "printf '%s' \"\$$var\"")
    if [ -n "$current" ]; then
        export "$var"="$dir:$current"
    else
        export "$var"="$dir"
    fi
}

[ -d "$APP_ROOT/usr/lib" ] && prepend_path LD_LIBRARY_PATH "$APP_ROOT/usr/lib"

for p in "$APP_ROOT/usr/plugins" \
         "$APP_ROOT/usr/lib/qt6/plugins" \
         "$APP_ROOT/usr/lib/qt5/plugins" \
         "$APP_ROOT/usr/lib/qt/plugins"; do
    [ -d "$p" ] && prepend_path QT_PLUGIN_PATH "$p"
done

for p in "$APP_ROOT/usr/qml" \
         "$APP_ROOT/usr/lib/qt6/qml" \
         "$APP_ROOT/usr/lib/qt5/qml" \
         "$APP_ROOT/usr/lib/qml"; do
    [ -d "$p" ] && prepend_path QML2_IMPORT_PATH "$p"
done

[ -d "$APP_ROOT/usr/share" ] && prepend_path XDG_DATA_DIRS "$APP_ROOT/usr/share"

BINARY="$APP_ROOT/usr/bin/Beacon"

# Validate binary before executing.
# If the binary is not a valid ELF executable, the kernel would try to
# interpret it as a script, causing "argument list too long" errors.
if [ ! -f "$BINARY" ]; then
    echo "Beacon: binary not found at $BINARY" >&2
    exit 1
fi

if [ ! -x "$BINARY" ]; then
    echo "Beacon: binary not executable at $BINARY (permissions: $(stat -c '%A' "$BINARY" 2>/dev/null || echo 'unknown'))" >&2
    exit 1
fi

# Check ELF magic bytes (0x7f 'E' 'L' 'F')
ELF_MAGIC=$(dd if="$BINARY" bs=4 count=1 2>/dev/null | od -A n -t x1 | tr -d ' \n' | cut -c1-8)
if [ "$ELF_MAGIC" != "7f454c46" ]; then
    echo "Beacon: binary at $BINARY is not a valid ELF executable (magic: $ELF_MAGIC)" >&2
    echo "Beacon: this usually means the AppImage was built incorrectly." >&2
    echo "Beacon: please rebuild with: python pack.py" >&2
    exit 1
fi

exec "$BINARY" "$@"
