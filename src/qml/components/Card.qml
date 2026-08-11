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
import QtQuick.Layouts

Rectangle {
    id: root

    property string variant: "elevated"
    property var cardContent: null

    implicitWidth: 300
    implicitHeight: contentItem ? contentItem.implicitHeight + 32 : 120

    radius: Theme.shapeMedium
    color: {
        switch (variant) {
            case "elevated": return Theme.surfaceContainerLow
            case "filled": return Theme.surfaceContainerHigh
            case "outlined": return Theme.surface
            default: return Theme.surfaceContainerLow
        }
    }

    border.width: variant === "outlined" ? 1 : 0
    border.color: variant === "outlined" ? Theme.outlineVariant : "transparent"

    layer.enabled: variant === "elevated"
    layer.effect: elevationEffect

    Behavior on color { ColorAnimation { duration: 200 } }

    property var elevationEffect: Component {
        Item {
            Rectangle {
                anchors.fill: parent
                anchors.margins: -4
                radius: root.radius + 4
                color: Qt.rgba(0,0,0,0.08)
                z: -1
            }
        }
    }

    default property alias content: contentArea.children

    ColumnLayout {
        id: contentArea
        x: 16; y: 16
        width: parent.width - 32
        height: parent.height - 32
        spacing: 8
    }
}
