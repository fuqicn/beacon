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

// Non-blocking update prompt docked at the bottom-right corner. It never
// steals focus or blocks interaction; while an update downloads, the prompt
// hides itself and progress moves to the unified download status panel.
Popup {
    id: root
    modal: false
    closePolicy: Popup.NoAutoClose
    focus: false
    padding: 0
    width: 340

    parent: Overlay.overlay
    x: parent.width - width - 24
    // Bottom offset is bound from main.qml so the prompt stacks above the
    // download status panel / compact nav bar instead of overlapping them.
    property real bottomMargin: 24
    y: parent.height - height - bottomMargin

    visible: kernel.updateAvailable && !kernel.updateDownloading && !dismissed
    property bool dismissed: false

    background: Rectangle {
        radius: Theme.shapeExtraLarge
        color: Qt.alpha(palette.window, Theme.transparencyOpacity)
        border.color: Qt.alpha(palette.mid, 0.4)
        border.width: 1
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            AppIcon {
                iconName: "download"
                iconSize: 16
                Layout.alignment: Qt.AlignVCenter
            }

            Text {
                Layout.fillWidth: true
                text: I18n.tr("update.title").replace("%1", kernel.latestVersion)
                font.pixelSize: 13; font.weight: Font.DemiBold
                color: palette.text
                elide: Text.ElideRight
            }

            Button {
                flat: true
                implicitWidth: 28; implicitHeight: 28
                onClicked: root.dismissed = true
                contentItem: Text {
                    text: "\u2715"
                    font.pixelSize: 12
                    color: palette.placeholderText
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: I18n.tr("update.later")
            }
        }

        Text {
            Layout.fillWidth: true
            text: I18n.tr("update.desc")
            font.pixelSize: 12
            color: palette.placeholderText
            wrapMode: Text.WordWrap
            lineHeight: 1.3
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Item { Layout.fillWidth: true }

            Button {
                text: I18n.tr("update.cancel")
                onClicked: {
                    kernel.cancelUpdate()
                    root.dismissed = true
                }
            }

            Button {
                text: I18n.tr("update.download")
                highlighted: true
                onClicked: {
                    root.dismissed = true
                    kernel.downloadUpdate()
                }
            }
        }
    }
}
