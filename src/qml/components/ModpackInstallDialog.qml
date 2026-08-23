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

Popup {
    id: root

    property var file: ({})
    property string projectName: ""
    property string logoUrl: ""
    property bool loading: true
    property string targetDir: ""

    signal installed(string message)

    readonly property bool fileReady: !root.loading && root.file
                                     && root.file.fileName
                                     && root.file.downloadUrl

    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    width: Math.min(parent.width - 64, 460)
    height: contentColumn.implicitHeight + 64
    padding: 24

    background: Rectangle {
        radius: Theme.shapeLarge
        color: palette.window
        border.color: palette.mid
        border.width: 1
    }

    contentItem: ColumnLayout {
        id: contentColumn
        spacing: 16

        // Loading state (version info still being fetched)
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 12
            visible: !root.fileReady

            BusyIndicator {
                Layout.alignment: Qt.AlignHCenter
                running: true
                implicitWidth: 36
                implicitHeight: 36
            }

            Text {
                Layout.alignment: Qt.AlignHCenter
                text: I18n.tr("modpackInstall.loading")
                font.pixelSize: 12
                color: palette.placeholderText
            }
        }

        // Ready state
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 16
            visible: root.fileReady

            Text {
                text: I18n.tr("modpackInstall.title").replace("%1", root.projectName || "Modpack")
                font.pixelSize: 18
                font.weight: Font.Bold
                color: palette.text
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            Text {
                text: root.file.fileName || ""
                font.pixelSize: 12
                color: palette.placeholderText
                elide: Text.ElideRight
                Layout.fillWidth: true
                visible: text !== ""
            }

            Text {
                text: I18n.tr("modpackInstall.desc")
                font.pixelSize: 11
                color: palette.placeholderText
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Layout.alignment: Qt.AlignRight

                Item { Layout.fillWidth: true }

                Button {
                    text: I18n.tr("modpackInstall.cancel")
                    font.weight: Font.Normal
                    onClicked: root.close()
                }

                Button {
                    text: I18n.tr("modpackInstall.start")
                    font.weight: Font.Normal
                    highlighted: true
                    onClicked: {
                        if (root.targetDir.length > 0) {
                            var f = root.file
                            f.iconUrl = root.logoUrl || ""
                            kernel.modpackManager.installFromProject(f, root.targetDir)
                            root.installed(I18n.tr("modpackInstall.started"))
                        }
                        root.close()
                    }
                }
            }
        }
    }
}
