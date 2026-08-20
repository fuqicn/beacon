#!/bin/sh
# Beacon payload AppImage AppRun (POSIX sh)
#
# Sets up the Qt runtime environment relative to the AppImage mount ($APPDIR)
# and launches the bundled binary. Used by beacon-app.AppImage so the app runs
# with its own Qt libraries/plugins/QML instead of relying on system ones.
set -e

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

[ -d "$APP_ROOT/usr/bin" ] && prepend_path PATH "$APP_ROOT/usr/bin"
[ -d "$APP_ROOT/usr/share" ] && prepend_path XDG_DATA_DIRS "$APP_ROOT/usr/share"

exec "$APP_ROOT/usr/bin/Beacon" "$@"