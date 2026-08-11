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

    property int currentIndex: 0
    property var model: []
    signal activated(int index)

    implicitHeight: 80
    color: Theme.surface

    Row {
        anchors.fill: parent
        Repeater {
            model: root.model
            delegate: Item {
                width: parent.width / root.model.length
                height: parent.height

                Column {
                    anchors.centerIn: parent
                    spacing: 4

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: modelData.icon || ""
                        font.family: "Material Symbols Outlined"
                        font.pixelSize: 24
                        color: index === root.currentIndex ? Theme.primary : Theme.onSurfaceVariant
                    }

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: modelData.label || ""
                        font.pixelSize: 12
                        font.bold: index === root.currentIndex
                        color: index === root.currentIndex ? Theme.primary : Theme.onSurfaceVariant
                    }
                }

                Rectangle {
                    anchors.top: parent.top
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 64
                    height: 3
                    radius: Theme.shapeExtraSmall
                    color: index === root.currentIndex ? Theme.primary : "transparent"
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        root.currentIndex = index
                        root.activated(index)
                    }
                    cursorShape: Qt.PointingHandCursor
                }
            }
        }
    }
}
