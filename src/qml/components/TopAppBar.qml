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

Rectangle {
    id: root

    property string title: "Title"
    property string subtitle: ""
    property string variant: "small"
    property var navAction: null

    implicitWidth: parent.width
    implicitHeight: variant === "compact" ? 48 : 64

    color: Theme.surface

    Row {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 16
        spacing: 4

        Item {
            width: childrenRect.width
            height: parent.height
            visible: root.navAction !== null
            Loader {
                anchors.centerIn: parent
                sourceComponent: root.navAction
            }
        }

        Column {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 0
            Text {
                text: root.title
                font.weight: variant === "medium" || variant === "large" ? Font.Normal : Font.Medium
                font.pixelSize: variant === "large" ? 28 : (variant === "medium" ? 24 : 22)
                color: Theme.onSurface
                lineHeight: 1.2
            }
            Text {
                text: root.subtitle
                font.pixelSize: 14
                color: Theme.onSurfaceVariant
                visible: root.subtitle !== ""
            }
        }
    }
}
