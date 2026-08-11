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
import QtQuick.Controls.impl

Rectangle {
    id: root

    property string variant: "filled"
    property string label: "Button"
    property bool buttonEnabled: true
    property bool flat: false
    property bool fullWidth: false

    signal clicked()

    implicitWidth: fullWidth ? parent.width : content.implicitWidth + 32
    implicitHeight: 40

    radius: Theme.shapeFull
    color: {
        if (!buttonEnabled) return Theme.onSurfaceVariant
        switch (variant) {
            case "filled": return Theme.primary
            case "tonal": return Theme.secondaryContainer
            case "outlined": return "transparent"
            case "text": return "transparent"
            default: return Theme.primary
        }
    }
    border.width: variant === "outlined" ? 1 : 0
    border.color: !buttonEnabled ? Theme.outlineVariant : Theme.outline

    opacity: buttonEnabled ? 1.0 : 0.38

    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        color: ma.pressed ? Qt.rgba(0,0,0,0.12) : (ma.hovered ? Qt.rgba(0,0,0,0.08) : "transparent")
        Behavior on color { ColorAnimation { duration: 150 } }
    }

    Text {
        id: content
        anchors.centerIn: parent
        text: root.label
        font.weight: Font.DemiBold
        font.pixelSize: 14
        color: {
            if (!buttonEnabled) return Theme.surface
            switch (variant) {
                case "filled": return Theme.onPrimary
                case "tonal": return Theme.onSecondaryContainer
                case "outlined": return Theme.primary
                case "text": return Theme.primary
                default: return Theme.onPrimary
            }
        }
        lineHeight: 1.0
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: if (root.buttonEnabled) root.clicked()
    }
}
