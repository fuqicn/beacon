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

// ScrollBar that auto-hides when idle (overlay behavior) on styles whose
// AsNeeded policy keeps the bar visible while content overflows (Windows 10 /
// Fusion). When the "always show scrollbars" setting is on it stays pinned.
ScrollBar {
    id: root

    // When true this bar follows Theme.alwaysScrollbars (AlwaysOn when on);
    // otherwise it is always AsNeeded but still auto-hides when idle.
    property bool followAlways: false

    policy: root.followAlways
            ? (Theme.alwaysScrollbars ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded)
            : ScrollBar.AsNeeded

    readonly property bool pinned: root.followAlways && Theme.alwaysScrollbars

    property real targetOpacity: 1
    opacity: root.targetOpacity
    Behavior on opacity { NumberAnimation { duration: 150 } }

    visible: root.size < 1 || root.pinned

    onActiveChanged: root.updateOpacity()
    onHoveredChanged: root.updateOpacity()
    onPinnedChanged: root.updateOpacity()

    // Keep the bar visible for a moment after scrolling stops.
    Timer {
        id: hideTimer
        interval: 400
        onTriggered: root.updateOpacity()
    }

    function updateOpacity() {
        if (root.pinned) {
            root.targetOpacity = 1
            return
        }
        if (root.active || root.hovered) {
            root.targetOpacity = 1
            hideTimer.restart()
        } else {
            root.targetOpacity = 0
        }
    }
}