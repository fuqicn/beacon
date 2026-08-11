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

    property string label: "Label"
    property string text: ""
    property string placeholderText: ""
    property bool outlined: true
    property bool readOnly: false

    signal textChanged(string newText)

    implicitWidth: 280
    implicitHeight: 56

    radius: Theme.shapeSmall
    color: outlined ? "transparent" : Theme.surfaceContainerHigh
    border.width: outlined ? 1 : 0
    border.color: activeFocus ? Theme.primary : Theme.outline

    TextInput {
        id: input
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        anchors.topMargin: 20
        anchors.bottomMargin: 8
        text: root.text
        font.pixelSize: 16
        color: Theme.onSurface
        readOnly: root.readOnly
        clip: true
        verticalAlignment: TextInput.AlignVCenter

        onTextChanged: root.textChanged(text)

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: parent.activeFocus ? 2 : 0
            color: Theme.primary
            visible: root.outlined ? false : parent.activeFocus
        }
    }

    Text {
        anchors.left: input.left
        anchors.bottom: input.top
        anchors.bottomMargin: activeFocus ? -16 : 4
        text: root.label
        font.pixelSize: activeFocus || input.text ? 12 : 16
        color: activeFocus ? Theme.primary : Theme.onSurfaceVariant
        Behavior on font.pixelSize { NumberAnimation { duration: 100 } }
        Behavior on anchors.bottomMargin { NumberAnimation { duration: 100 } }
    }
}
