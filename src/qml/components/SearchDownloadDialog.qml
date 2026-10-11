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
    property string name: ""
    property string type: "resourcepack"   // resourcepack | shader | datapack
    property string targetDir: ""

    signal installed(string message)
    signal confirmInstall(string dir)

    readonly property bool fileReady: root.file
                                      && root.file.fileName
                                      && root.file.downloadUrl

    // 当前选中的实例
    property int selectedInstIndex: -1

    // 版本不存在警告对话框
    Dialog {
        id: versionNotFoundDialog
        parent: root
        modal: true
        title: I18n.tr("searchResource.versionNotFound").replace("%1", "")
        standardButtons: Dialog.Cancel | Dialog.Ok
        closePolicy: Popup.CloseOnEscape

        onAccepted: {
            // 用户选择继续，执行下载
            var selInst = kernel.instanceManager.instances[root.selectedInstIndex]
            var versionId = selInst ? selInst.id : ""
            var targetDir = versionId && selInst && selInst.rootDir
                          ? kernel.gameDirFor(selInst.rootDir, versionId)
                          : root.targetDir || kernel.mcDir

            var r = Object.assign({}, root.file)
            r["name"] = root.name
            r["type"] = root.type
            kernel.searchManager.installResult(r, targetDir)
            root.installed(I18n.tr("modDownload.added"))
            root.close()
        }
        onRejected: { root.close() }
    }

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

        // Loading state
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
                text: I18n.tr("modDownload.loading")
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
                text: root.name || root.file.fileName || ""
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

            // Minecraft 目录选择下拉列表
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    text: I18n.tr("searchResource.instances")
                    font.pixelSize: 13
                    color: palette.placeholderText
                }

                ComboBox {
                    id: instCombo
                    Layout.fillWidth: true
                    model: kernel.instanceManager.rootDirs.length > 0
                           ? kernel.instanceManager.rootDirs
                           : [kernel.mcDir]
                    currentIndex: _findCurrentIndex()
                    enabled: true

                    function _findCurrentIndex() {
                        var dirs = model || []
                        var current = kernel.instanceManager.currentRootDir || kernel.mcDir
                        for (var i = 0; i < dirs.length; ++i) {
                            if (dirs[i] === current) return i
                        }
                        return 0
                    }

                    onCurrentIndexChanged: {
                        if (currentIndex >= 0 && currentIndex < model.length) {
                            root.targetDir = model[currentIndex]
                            root.selectedInstIndex = currentIndex
                        }
                    }
                }
            }

            // 提示文字
            Text {
                text: {
                    if (root.type === "resourcepack")
                        return I18n.tr("searchResource.installDesc").replace("%1", "resourcepacks")
                    if (root.type === "shader")
                        return I18n.tr("searchResource.installDesc").replace("%1", "shaderpacks")
                    return I18n.tr("searchResource.installDesc").replace("%1", "datapacks")
                }
                font.pixelSize: 11
                color: palette.placeholderText
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Layout.alignment: Qt.AlignRight

                Item { Layout.fillWidth: true }

                Button {
                    text: I18n.tr("modDownload.download")
                    font.weight: Font.Normal
                    highlighted: true
                    onClicked: {
                        // 获取选中的目录
                        var selDir = root.targetDir
                        if (!selDir || selDir.length === 0) {
                            selDir = kernel.instanceManager.currentRootDir || kernel.mcDir
                        }

                        // 检查是否有选中的实例
                        var selInst = kernel.instanceManager.instances[root.selectedInstIndex]
                        var versionId = selInst ? selInst.id : ""

                        // 如果用户选择了实例但没有版本ID，弹窗提示
                        if (root.selectedInstIndex >= 0 && (!versionId || versionId === "")) {
                            versionNotFoundDialog.open()
                            return
                        }

                        // 如果用户没有选择实例（selectedInstIndex = -1），弹窗提示
                        if (root.selectedInstIndex < 0) {
                            versionNotFoundDialog.open()
                            return
                        }

                        // 计算实际目标目录（应用版本隔离）
                        var targetDir = versionId && selInst && selInst.rootDir
                                      ? kernel.gameDirFor(selInst.rootDir, versionId)
                                      : selDir

                        // 执行下载
                        var r = Object.assign({}, root.file)
                        r["name"] = root.name
                        r["type"] = root.type
                        kernel.searchManager.installResult(r, targetDir)
                        root.installed(I18n.tr("modDownload.added"))
                        root.close()
                    }
                }
            }
        }
    }

    Connections {
        target: kernel.instanceManager
        function onInstancesChanged() {
            // 实例列表变化时重置选择
            if (root.selectedInstIndex >= kernel.instanceManager.instances.length)
                root.selectedInstIndex = -1
        }
    }
}
