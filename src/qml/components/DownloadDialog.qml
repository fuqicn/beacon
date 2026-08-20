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

    property string versionId: ""
    property string versionType: ""
    property string downloadDir: ""

    // True while downloading post-install files (libraries + assets)
    property bool postInstallDownload: false
    property string installedLoaderVersionId: ""

    onOpened: {
        root.downloadDir = kernel.instanceManager.currentRootDir || kernel.mcDir
    }

    FolderDialog {
        id: dirDialog
        currentFolder: "file:///" + (root.downloadDir || kernel.mcDir)
        onAccepted: {
            if (selectedFolder)
                root.downloadDir = selectedFolder.toString().replace(/^file:\/\//, "")
        }
    }

    modal: true
    focus: true
    closePolicy: postInstallDownload ? Popup.NoAutoClose : Popup.CloseOnEscape
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    width: Math.min(parent.width - 64, 500)
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
        spacing: 20

        Text {
            text: postInstallDownload
                  ? "下载 " + root.installedLoaderVersionId
                  : "下载 " + root.versionId
            font.pixelSize: 22
            font.weight: Font.Bold
            color: palette.text
        }

        Text {
            text: postInstallDownload
                  ? "类型: " + (root.versionType !== "" ? root.versionType : "loader")
                  : "类型: " + root.versionType
            font.pixelSize: 13
            color: palette.placeholderText
            visible: text !== "" && !postInstallDownload
        }

        // Download directory picker
        RowLayout {
            spacing: 12
            visible: !postInstallDownload

            Text {
                text: "下载目录"
                font.pixelSize: 14
                color: palette.placeholderText
            }

            Text {
                id: downloadDirText
                text: root.downloadDir
                Layout.fillWidth: true
                elide: Text.ElideMiddle
                font.pixelSize: 13
                color: palette.text
            }

            Button {
                text: "选择..."
                font.weight: Font.Normal
                onClicked: dirDialog.open()
            }
        }

        // Install mode UI (loader selection)
        ColumnLayout {
            spacing: 12
            visible: !postInstallDownload

            RowLayout {
                spacing: 12

                Text {
                    text: "安装类型"
                    font.pixelSize: 14
                    color: palette.placeholderText
                }

                ComboBox {
                    id: loaderCombo
                    Layout.fillWidth: true
                    font.weight: Font.Medium
                    model: ListModel {
                        ListElement { text: "原版"; loader: "" }
                        ListElement { text: "Fabric"; loader: "fabric" }
                        ListElement { text: "Forge"; loader: "forge" }
                        ListElement { text: "Quilt"; loader: "quilt" }
                        ListElement { text: "NeoForge"; loader: "neoforge" }
                    }
                    textRole: "text"
                    currentIndex: 0
                    delegate: ItemDelegate {
                        width: parent ? parent.width : 0
                        text: model.text
                        font.weight: Font.Medium
                    }
                    onCurrentIndexChanged: {
                        var loader = model.get(currentIndex).loader
                        if (loader !== "") {
                            loaderVersionCombo.visible = true
                            loaderVersionCombo.model.clear()
                            loaderVersionCombo.model.append({ text: "正在获取...", value: "" })
                            loaderVersionCombo.currentIndex = 0
                            kernel.installManager.fetchLoaderVersions(root.versionId, loader)
                        } else {
                            loaderVersionCombo.visible = false
                        }
                    }
                }
            }

            RowLayout {
                spacing: 12
                visible: loaderCombo.currentIndex > 0

                Text {
                    text: "Loader 版本"
                    font.pixelSize: 14
                    color: palette.placeholderText
                }

                ComboBox {
                    id: loaderVersionCombo
                    Layout.fillWidth: true
                    font.weight: Font.Medium
                    model: ListModel { }
                    textRole: "text"
                    currentIndex: 0
                    delegate: ItemDelegate {
                        width: parent ? parent.width : 0
                        text: model.text
                        font.weight: Font.Medium
                    }
                }

                Button {
                    id: retryVersionsBtn
                    visible: false
                    text: "重试"
                    onClicked: {
                        retryVersionsBtn.visible = false
                        loaderVersionCombo.model.clear()
                        loaderVersionCombo.model.append({ text: "正在获取...", value: "" })
                        loaderVersionCombo.currentIndex = 0
                        kernel.installManager.fetchLoaderVersions(
                                    root.versionId,
                                    loaderCombo.model.get(loaderCombo.currentIndex).loader)
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: palette.mid
                opacity: 0.3
            }

            ColumnLayout {
                spacing: 8

                Text {
                    text: "下载说明"
                    font.pixelSize: 12
                    color: palette.placeholderText
                }

                Text {
                    text: "前 2 次使用 Mojang 官方源，第 3 次使用自动源。\n任何一次成功即停止。3 次均失败则提示错误。"
                    font.pixelSize: 12
                    color: palette.placeholderText
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    lineHeight: 1.4
                }
            }
        }

        // Status area (both install and download modes)
        Text {
            id: installStatus
            text: ""
            font.pixelSize: 12
            color: palette.placeholderText
            visible: text !== ""
        }

        BusyIndicator {
            id: installSpinner
            width: 20; height: 20
            visible: false
            Layout.alignment: Qt.AlignHCenter
        }

        // Download progress bar (visible during post-install download)
        ColumnLayout {
            spacing: 6
            visible: postInstallDownload
            Layout.fillWidth: true

            Rectangle {
                id: progressTrack
                Layout.fillWidth: true
                height: 6
                radius: Theme.shapeExtraSmall
                color: Qt.alpha(Theme.primary, 0.12)
                clip: true

                Rectangle {
                    id: progressBar
                    width: parent.width * Math.min(downloadProgress, 1.0)
                    height: parent.height
                    radius: Theme.shapeExtraSmall
                    color: Theme.primary
                    Behavior on width { SmoothedAnimation { duration: 300; velocity: 200 } }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                Text {
                    text: Math.round(downloadProgress * 100) + "%"
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    color: palette.text
                }

                Text {
                    text: downloadTotal > 0
                          ? downloadCompleted + "/" + downloadTotal + " 文件"
                          : ""
                    font.pixelSize: 11
                    color: palette.placeholderText
                    Layout.fillWidth: true
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Item { Layout.fillWidth: true }

            Button {
                id: cancelBtn
                text: postInstallDownload ? "关闭" : "取消"
                font.weight: Font.Normal
                enabled: !postInstallDownload || !kernel.downloadManager.busy
                onClicked: {
                    if (postInstallDownload && kernel.downloadManager.busy)
                        kernel.downloadManager.cancelAll()
                    root.close()
                }
            }

            Button {
                id: downloadBtn
                text: postInstallDownload ? "正在下载..." : "开始下载"
                font.weight: Font.Normal
                highlighted: !postInstallDownload
                enabled: !postInstallDownload
                visible: !postInstallDownload
                onClicked: {
                    var loader = loaderCombo.model.get(loaderCombo.currentIndex).loader
                    if (loader === "") {
                        kernel.downloadManager.startDownload(root.versionId, root.downloadDir)
                        root.close()
                    } else {
                        var lver = ""
                        if (loaderVersionCombo.currentIndex >= 0 && loaderVersionCombo.currentIndex < loaderVersionCombo.model.count)
                            lver = loaderVersionCombo.model.get(loaderVersionCombo.currentIndex).value
                        downloadBtn.enabled = false
                        cancelBtn.enabled = false
                        installSpinner.visible = true
                        installStatus.text = "正在安装 " + loader + "..."
                        kernel.installManager.installLoader(root.versionId, loader, lver, "", root.downloadDir)
                    }
                }
            }
        }
    }

    readonly property real downloadProgress: kernel.downloadManager.progress
    readonly property int downloadTotal: kernel.downloadManager.totalFiles
    readonly property int downloadCompleted: kernel.downloadManager.completedFiles

    Connections {
        target: kernel.installManager
        function onLoaderVersionsReady(versions, loader) {
            if (loader !== loaderCombo.model.get(loaderCombo.currentIndex).loader)
                return
            loaderVersionCombo.model.clear()
            for (var i = 0; i < versions.length; i++) {
                loaderVersionCombo.model.append({ text: versions[i], value: versions[i] })
            }
            if (versions.length > 0) {
                loaderVersionCombo.currentIndex = 0
                retryVersionsBtn.visible = false
            } else {
                // Empty result (e.g. network reset); offer a retry instead of a blank box
                loaderVersionCombo.model.append({ text: "获取失败，请重试", value: "" })
                loaderVersionCombo.currentIndex = 0
                retryVersionsBtn.visible = true
            }
        }
        function onInstallCompleted(versionId) {
            installStatus.text = "安装完成，正在下载资源文件..."
            installSpinner.visible = false
            downloadBtn.enabled = true
            cancelBtn.enabled = true
            root.installedLoaderVersionId = versionId
            root.postInstallDownload = true
            kernel.downloadManager.startDownload(versionId, root.downloadDir)
        }
        function onErrorOccurred(msg) {
            installStatus.text = "错误: " + msg
            installSpinner.visible = false
            downloadBtn.enabled = true
            cancelBtn.enabled = true
        }
        function onProgressChanged(p) {
            installStatus.text = kernel.installManager.status
        }
        function onStatusChanged(s) {
            installStatus.text = s
        }
    }

    Connections {
        target: kernel.downloadManager
        function onAllCompleted(success) {
            // Register the download target as an instance root and refresh the list
            if (root.downloadDir.length > 0)
                kernel.instanceManager.addRootDir(root.downloadDir)
            if (root.postInstallDownload) {
                if (success)
                    installStatus.text = "下载完成"
                else
                    installStatus.text = "部分文件下载失败"
                root.postInstallDownload = false
                root.close()
            }
        }
        function onProgressChanged(p, task) {
            if (root.postInstallDownload)
                installStatus.text = task
        }
        function onErrorOccurred(msg) {
            if (root.postInstallDownload) {
                installStatus.text = "下载错误: " + msg
            }
        }
    }
}
