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

Rectangle {
    id: root

    property string iconText: ""
    property string label: ""
    property string size: "medium"
    property bool extended: false

    signal clicked()

    implicitWidth: extended ? (labelText.implicitWidth + (iconText ? 56 : 32)) : (size === "small" ? 40 : (size === "large" ? 96 : 56))
    implicitHeight: size === "small" ? 40 : (size === "large" ? 96 : 56)

    radius: size === "large" ? Theme.shapeLarge : Theme.shapeLarge
    color: Theme.primaryContainer

    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        color: ma.pressed ? Qt.rgba(0,0,0,0.12) : (ma.hovered ? Qt.rgba(0,0,0,0.08) : "transparent")
        Behavior on color { ColorAnimation { duration: 150 } }
    }

    Row {
        anchors.centerIn: parent
        spacing: 8
        Text {
            id: iconTextItem
            text: root.iconText
            font.family: "Material Symbols Outlined"
            font.pixelSize: root.size === "small" ? 20 : 24
            color: Theme.onPrimaryContainer
            visible: root.iconText !== ""
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            id: labelText
            text: root.label
            font.weight: Font.DemiBold
            font.pixelSize: 14
            color: Theme.onPrimaryContainer
            visible: root.extended || root.label !== ""
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
