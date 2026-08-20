/*
 * Beacon - a cross-platform Minecraft launcher.
 *
 * Copyright (C) 2024-2026 fuqicn
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
import QtQuick
import QtQuick.Controls

QtObject {
    // darkMode follows the explicit theme mode; only in "system" mode does it
    // fall back to the palette's perceived brightness. This keeps the app in
    // the chosen scheme even on platforms whose palette doesn't flip on
    // setColorScheme (Windows 10, GNOME/Linux).
    readonly property bool darkMode: themeMode === "dark" ? true
                                     : themeMode === "light" ? false
                                     : (sysPalette.window.hslLightness < 0.5)

    property string themeMode: "system"
    property bool alwaysScrollbars: false
    property bool transparencyEnabled: true
    property bool animationsEnabled: true

    readonly property SystemPalette sysPalette: SystemPalette {}

    readonly property color primary: sysPalette.highlight
    readonly property color onPrimary: sysPalette.highlightedText
    readonly property color primaryContainer: Qt.alpha(sysPalette.highlight, 0.15)
    readonly property color onPrimaryContainer: sysPalette.text
    readonly property color secondary: sysPalette.highlight
    readonly property color onSecondary: sysPalette.highlightedText
    readonly property color secondaryContainer: Qt.alpha(sysPalette.highlight, 0.12)
    readonly property color onSecondaryContainer: sysPalette.text
    readonly property color surface: sysPalette.window
    readonly property color onSurface: sysPalette.text
    readonly property color onSurfaceVariant: sysPalette.placeholderText
    readonly property color surfaceContainerLowest: sysPalette.window
    readonly property color surfaceContainerLow: sysPalette.window
    readonly property color surfaceContainer: _surface(0.05)
    readonly property color surfaceContainerHigh: _surface(0.08)
    readonly property color surfaceContainerHighest: _surface(0.12)
    readonly property color outline: sysPalette.mid
    readonly property color outlineVariant: sysPalette.midlight

    // Rounded corners follow the active style: FluentWinUI3 / macOS / Imagine
    // are rounded; the traditional Windows and Fusion styles are square.
    readonly property bool _cornersEnabled: uiStyleName === "fluentwinui3"
                                            || uiStyleName === "macos"
                                            || uiStyleName === "imagine"

    readonly property real shapeNone: 0
    readonly property real shapeExtraSmall: _cornersEnabled ? 2 : 0
    readonly property real shapeSmall: _cornersEnabled ? 4 : 0
    readonly property real shapeMedium: _cornersEnabled ? 8 : 0
    readonly property real shapeLarge: _cornersEnabled ? 12 : 0
    readonly property real shapeExtraLarge: _cornersEnabled ? 16 : 0
    readonly property real shapeFull: _cornersEnabled ? 9999 : 0

    readonly property real shapeNavRail: _cornersEnabled ? 20 : 0

    // Page-transition style follows the style: FluentWinUI3 / macOS / Imagine
    // shrink+fade (Win11 default); traditional Windows and Fusion fade only.
    readonly property bool scaleAnim: uiStyleName === "fluentwinui3"
                                      || uiStyleName === "macos"
                                      || uiStyleName === "imagine"

    function _surface(t) {
        return transparencyEnabled
            ? Qt.alpha(sysPalette.placeholderText, t)
            : _mix(sysPalette.window, sysPalette.placeholderText, t)
    }

    function _mix(a, b, t) {
        return Qt.rgba(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1.0)
    }

    readonly property real transparencyOpacity: 0.75

    readonly property real dp4: 4
    readonly property real dp8: 8
    readonly property real dp12: 12
    readonly property real dp16: 16
    readonly property real dp24: 24
    readonly property real dp32: 32
    readonly property real dp48: 48

    signal themeChanged()
    onDarkModeChanged: themeChanged()

    Component.onCompleted: {
        var sm = kernel.settingsManager
        var mode = sm.value("theme/mode", "")
        if (mode === "") {
            var legacy = sm.value("theme/colorScheme", "")
            themeMode = (legacy === "light" || legacy === "dark") ? legacy : "system"
            if (themeMode !== "system")
                sm.setValue("theme/mode", themeMode)
        } else {
            themeMode = mode
        }
        alwaysScrollbars = sm.value("system/scrollbars", "false").toString() === "true"
        transparencyEnabled = sm.value("system/transparency", "true").toString() !== "false"
        animationsEnabled = sm.value("system/animations", "true").toString() !== "false"
        kernel.applyThemeMode(themeMode)
    }

    function setThemeMode(mode) {
        if (themeMode === mode) return
        themeMode = mode
        kernel.settingsManager.setValue("theme/mode", mode)
        kernel.applyThemeMode(mode)
        themeChanged()
    }

    function toggleTheme() {
        if (themeMode === "system")
            setThemeMode(darkMode ? "light" : "dark")
        else
            setThemeMode(themeMode === "dark" ? "light" : "dark")
    }

    function setAlwaysScrollbars(v) {
        alwaysScrollbars = v
        kernel.settingsManager.setValue("system/scrollbars", v)
    }

    function setTransparencyEnabled(v) {
        transparencyEnabled = v
        kernel.settingsManager.setValue("system/transparency", v)
        kernel.applyWindowTransparency(v)
    }

    function setAnimationsEnabled(v) {
        animationsEnabled = v
        kernel.settingsManager.setValue("system/animations", v)
    }
}
