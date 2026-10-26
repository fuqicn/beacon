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

// Version install dialog. It only collects the options (directory, instance
// name, loader) — starting either a download or a loader install closes it
// immediately. Progress and cancellation live in the bottom-right
// DownloadStatusPanel, which is always present, so nothing here needs to stay
// on screen while the job runs.
Popup {
    id: root

    property string versionId: ""
    property string versionType: ""
    property string downloadDir: ""
    property string customName: ""

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
    closePolicy: Popup.CloseOnEscape
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
            text: I18n.tr("download.title").replace("%1", root.versionId)
            font.pixelSize: 22
            font.weight: Font.Bold
            color: palette.text
        }

        Text {
            text: I18n.tr("download.typeLabel").replace("%1", root.versionType)
            font.pixelSize: 13
            color: palette.placeholderText
            visible: text !== ""
        }

        // Download directory picker
        RowLayout {
            spacing: 12

            Text {
                text: I18n.tr("download.dirLabel")
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
                text: I18n.tr("download.browse")
                font.weight: Font.Normal
                onClicked: dirDialog.open()
            }
        }

        // Install mode UI (loader selection)
        ColumnLayout {
            spacing: 12

            // Custom instance name
            RowLayout {
                spacing: 12

                Text {
                    text: I18n.tr("download.instanceName")
                    font.pixelSize: 14
                    color: palette.placeholderText
                }

                TextField {
                    id: customNameField
                    Layout.fillWidth: true
                    placeholderText: I18n.tr("download.instanceNamePlaceholder")
                    text: root.customName
                    onTextChanged: root.customName = text
                }
            }

            RowLayout {
                spacing: 12

                Text {
                    text: I18n.tr("download.installType")
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
                            loaderVersionCombo.model.append({ text: I18n.tr("download.fetching"), value: "" })
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
                    text: I18n.tr("download.loaderVersion")
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
                    text: I18n.tr("download.retry")
                    onClicked: {
                        retryVersionsBtn.visible = false
                        loaderVersionCombo.model.clear()
                        loaderVersionCombo.model.append({ text: I18n.tr("download.fetching"), value: "" })
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
                    text: I18n.tr("download.info")
                    font.pixelSize: 12
                    color: palette.placeholderText
                }

                Text {
                    text: I18n.tr("download.infoText")
                    font.pixelSize: 12
                    color: palette.placeholderText
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    lineHeight: 1.4
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Item { Layout.fillWidth: true }

            Button {
                id: cancelBtn
                text: I18n.tr("download.cancel")
                font.weight: Font.Normal
                onClicked: root.close()
            }

            Button {
                id: downloadBtn
                text: I18n.tr("download.start")
                font.weight: Font.Normal
                highlighted: true
                onClicked: {
                    var loader = loaderCombo.model.get(loaderCombo.currentIndex).loader
                    var lver = ""
                    if (loader !== ""
                            && loaderVersionCombo.currentIndex >= 0
                            && loaderVersionCombo.currentIndex < loaderVersionCombo.model.count)
                        lver = loaderVersionCombo.model.get(loaderVersionCombo.currentIndex).value
                    // The bottom-right status panel owns the progress UI for both
                    // paths (loader install → libraries/assets download), so this
                    // dialog is only a form: close it before the job starts, which
                    // also keeps the UI responsive while Forge prepares Java.
                    root.close()
                    if (loader === "") {
                        kernel.downloadManager.startDownload(root.versionId, root.downloadDir)
                    } else {
                        kernel.installManager.installLoader(
                                    root.versionId, loader, lver, "", root.downloadDir, root.customName)
                    }
                }
            }
        }
    }

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
                loaderVersionCombo.model.append({ text: I18n.tr("download.fetchFailed"), value: "" })
                loaderVersionCombo.currentIndex = 0
                retryVersionsBtn.visible = true
            }
        }
    }
}
