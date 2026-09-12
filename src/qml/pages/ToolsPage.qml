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
import "../components"

Item {
    id: root

    property bool memOptBusy: false

    Flickable {
        anchors.fill: parent
        contentHeight: contentColumn.implicitHeight + 48
        clip: true
        flickableDirection: Flickable.VerticalFlick

        ColumnLayout {
            id: contentColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 24
            spacing: 16

            Text {
                text: I18n.tr("tools")
                font.pixelSize: 18; font.weight: Font.Medium
                color: palette.text
            }

            Flow {
                Layout.fillWidth: true
                spacing: 16

                Repeater {
                model: [
                    { name: I18n.tr("tools.modSearch"), desc: I18n.tr("tools.modSearchDesc"), icon: "magnifying-glass", page: 1, sub: "" },
                    { name: I18n.tr("tools.javaManage"), desc: I18n.tr("tools.javaManageDesc"), icon: "coffee", page: 1, sub: "" },
                    { name: I18n.tr("tools.logView"), desc: I18n.tr("tools.logViewDesc"), icon: "file-lines", page: 6, sub: I18n.tr("tools.logSub") }
                ]

                Frame {
                    width: 220; height: 120

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (modelData.page === 1)
                                window.navigateTo(1)
                            else
                                window.navigateToPage(modelData.page, modelData.sub)
                        }
                    }

                    Column {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 8
                        anchors.verticalCenter: parent.verticalCenter

                        AppIcon {
                            iconName: modelData.icon
                            iconSize: 24
                        }
                            Text {
                                text: modelData.name
                                font.pixelSize: 15; font.weight: Font.Medium
                                color: palette.text
                                width: parent.width
                                elide: Text.ElideRight
                            }
                            Text {
                                text: modelData.desc
                                font.pixelSize: 12
                                color: palette.placeholderText
                                wrapMode: Text.WordWrap
                                width: parent.width
                            }
                        }
                    }
                }
            }

            // Download Java section
            Text {
                text: I18n.tr("java.download")
                font.pixelSize: 18; font.weight: Font.Medium
                color: palette.text
                Layout.topMargin: 24
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: javaDlInner.implicitHeight + 32
                radius: Theme.shapeMedium
                color: Theme.surfaceContainer
                RowLayout {
                    id: javaDlInner
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12

                    Text { text: I18n.tr("java.version"); color: palette.placeholderText; font.pixelSize: 14 }
                    Item { Layout.fillWidth: true }
                    ComboBox {
                        id: javaVerCombo
                        model: ["8", "11", "16", "17", "21", "25"]
                        currentIndex: 2
                    }
                    Button {
                        visible: !kernel.downloadManager.busy
                        text: I18n.tr("java.downloadBtn")
                        highlighted: true
                        font.weight: Font.Normal
                        onClicked: {
                            var ver = parseInt(javaVerCombo.currentText)
                            kernel.downloadManager.downloadJava(ver, kernel.instanceManager.currentRootDir || kernel.mcDir)
                        }
                    }
                    Button {
                        visible: kernel.downloadManager.busy
                        text: I18n.tr("java.cancel")
                        onClicked: kernel.downloadManager.cancelAll()
                    }
                }
            }

            Text {
                text: kernel.downloadManager.currentTask
                font.pixelSize: 12; color: palette.placeholderText
                visible: kernel.downloadManager.busy
            }

            ProgressBar {
                visible: kernel.downloadManager.busy
                from: 0; to: kernel.downloadManager.totalFiles
                value: kernel.downloadManager.completedFiles
                Layout.fillWidth: true
            }

            Text {
                id: javaStatusText
                font.pixelSize: 12; color: palette.placeholderText
                visible: text.length > 0
            }

            // Memory optimization section
            Text {
                text: I18n.tr("tools.memOpt")
                font.pixelSize: 18; font.weight: Font.Medium
                color: palette.text
                Layout.topMargin: 24
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: memOptInner.implicitHeight + 32
                radius: Theme.shapeMedium
                color: Theme.surfaceContainer
                RowLayout {
                    id: memOptInner
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12

                    Text {
                        text: I18n.tr("tools.memOptDesc")
                        color: palette.placeholderText
                        font.pixelSize: 13
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    Item { Layout.fillWidth: true }
                    Button {
                        text: I18n.tr("tools.memOpt")
                        highlighted: true
                        font.weight: Font.Normal
                        enabled: !memOptBusy
                        onClicked: memOptConfirm.open()
                    }
                }
            }

            Text {
                id: memOptStatus
                text: ""
                font.pixelSize: 12; color: palette.placeholderText
                visible: text.length > 0
            }

            Popup {
                id: memOptConfirm
                modal: true
                focus: true
                closePolicy: Popup.CloseOnEscape
                x: Math.round((parent.width - width) / 2)
                y: Math.round((parent.height - height) / 2)
                width: Math.min(parent.width - 64, 420)
                padding: 24
                background: Rectangle {
                    radius: Theme.shapeLarge
                    color: palette.window
                    border.color: palette.mid
                    border.width: 1
                }
                contentItem: ColumnLayout {
                    spacing: 16
                    Text {
                        text: I18n.tr("tools.memOpt.confirmTitle")
                        font.pixelSize: 18; font.weight: Font.Bold
                        color: palette.text
                    }
                    Text {
                        text: I18n.tr("tools.memOpt.confirmText")
                        font.pixelSize: 13
                        color: palette.placeholderText
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                        lineHeight: 1.4
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        Layout.alignment: Qt.AlignRight
                        Button {
                            text: I18n.tr("cancel")
                            font.weight: Font.Normal
                            onClicked: memOptConfirm.close()
                        }
                        Button {
                            text: I18n.tr("delete")
                            highlighted: true
                            font.weight: Font.Normal
                            onClicked: {
                                memOptConfirm.close()
                                memOptBusy = true
                                memOptStatus.text = I18n.tr("tools.memOpt.optimizing")
                                // Defer to the next event loop iteration so the
                                // "optimizing" label paints before the blocking GC.
                                Qt.callLater(function() {
                                    kernel.launcherMemoryOptimize()
                                    memOptBusy = false
                                    memOptStatus.text = I18n.tr("tools.memOpt.done")
                                })
                            }
                        }
                    }
                }
            }

            Item { Layout.fillHeight: true }
        }

        ScrollBar.vertical: OverlayScrollBar {
            followAlways: true
            policy: Theme.alwaysScrollbars ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
            width: 8
        }
    }

    Connections {
        target: kernel.downloadManager
        function onJavaDownloaded(majorVersion, javaPath) {
            kernel.javaManager.addBundledJava();
            javaStatusText.text = I18n.tr("java.downloaded").replace("%1", majorVersion)
        }
        function onErrorOccurred(message) {
            if (message.indexOf("Java") >= 0)
                javaStatusText.text = message
        }
    }
}
