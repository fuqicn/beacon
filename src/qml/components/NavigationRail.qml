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

    implicitWidth: 80
    color: Theme.surface

    Column {
        anchors.fill: parent
        anchors.topMargin: 12
        spacing: 8

        Repeater {
            model: root.model
            delegate: Item {
                width: root.width
                height: 72

                Column {
                    anchors.centerIn: parent
                    spacing: 4

                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 56
                        height: 32
                        radius: Theme.shapeExtraLarge
                        color: index === root.currentIndex ? Theme.secondaryContainer : "transparent"

                        Text {
                            anchors.centerIn: parent
                            text: modelData.icon || ""
                            font.pixelSize: 24
                            color: index === root.currentIndex ? Theme.onSecondaryContainer : Theme.onSurfaceVariant
                        }
                    }

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: modelData.label || ""
                        font.pixelSize: 12
                        font.bold: index === root.currentIndex
                        color: index === root.currentIndex ? Theme.onSecondaryContainer : Theme.onSurfaceVariant
                    }
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
