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

Dialog {
    id: root
    modal: true
    focus: true
    standardButtons: Dialog.None
    anchors.centerIn: Overlay.overlay
    width: Math.min(480, Window.width * 0.9)
    height: contentHeight + 48

    function _curVer() {
        var v = kernel.readVersion()
        return v.startsWith("v") ? v.mid(1) : v
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        Text {
            text: I18n.tr("update.title").replace("%1", kernel.latestVersion).replace("%2", _curVer())
            font.pixelSize: 16; font.weight: Font.Medium
            color: palette.text
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }

        Text {
            text: I18n.tr("update.desc")
            font.pixelSize: 13
            color: palette.placeholderText
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }

        BusyIndicator {
            Layout.alignment: Qt.AlignHCenter
            running: kernel.updateDownloadProgress !== ""
            visible: running
        }

        Text {
            text: kernel.updateDownloadProgress
            font.pixelSize: 12
            color: palette.placeholderText
            Layout.alignment: Qt.AlignHCenter
            visible: kernel.updateDownloadProgress !== ""
        }

        Item { Layout.fillHeight: true }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            anchors.right: parent.right
            Item { Layout.fillWidth: true }
            Button {
                text: I18n.tr("update.cancel")
                onClicked: { kernel.cancelUpdate(); root.close() }
            }
            Button {
                text: I18n.tr("update.download")
                enabled: kernel.updateDownloadProgress === ""
                onClicked: {
                    kernel.downloadUpdate()
                    root.close()
                }
            }
        }
    }
}
