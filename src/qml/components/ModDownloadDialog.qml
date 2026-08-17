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
import QtQuick.Dialogs

Popup {
    id: root

    property var file: ({})
    property bool loading: true
    property string targetDir: ""

    signal installed(string message)

    readonly property bool fileReady: !root.loading && root.file
                                     && root.file.fileName
                                     && root.file.downloadUrl

    FolderDialog {
        id: dirDialog
        currentFolder: "file:///" + (root.targetDir || kernel.mcDir)
        onAccepted: {
            if (selectedFolder)
                root.targetDir = selectedFolder.toString().replace(/^file:\/\//, "")
        }
    }

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
                text: "正在加载版本信息..."
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
                text: "下载 " + (root.file.displayName || root.file.fileName || "模组")
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

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    text: "下载目录"
                    font.pixelSize: 13
                    color: palette.placeholderText
                }

                Text {
                    id: dirLabel
                    Layout.fillWidth: true
                    text: root.targetDir
                    font.pixelSize: 12
                    color: palette.text
                    elide: Text.ElideMiddle
                }

                Button {
                    text: "选择..."
                    font.weight: Font.Normal
                    onClicked: dirDialog.open()
                }
            }

            Text {
                text: "模组将安装到所选目录的 mods 文件夹"
                font.pixelSize: 11
                color: palette.placeholderText
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Layout.alignment: Qt.AlignRight

                Item { Layout.fillWidth: true }

                Button {
                    text: "取消"
                    font.weight: Font.Normal
                    onClicked: root.close()
                }

                Button {
                    text: "下载"
                    font.weight: Font.Normal
                    highlighted: true
                    onClicked: {
                        if (root.targetDir.length > 0) {
                            kernel.modManager.installMod(root.file, root.targetDir)
                            root.installed("已加入下载队列，可在下方下载面板查看进度")
                        }
                        root.close()
                    }
                }
            }
        }
    }
}
