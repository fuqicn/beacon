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

    Connections {
        target: kernel
        function onMemOptimizingChanged() {
            javaStatusText.text = kernel.memOptimizing
                ? I18n.tr("tools.memOpt.optimizing")
                : I18n.tr("tools.memOpt.done")
        }
    }

    Flickable {
        id: flick
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
                        { name: I18n.tr("tools.logView"), desc: I18n.tr("tools.logViewDesc"), icon: "file-lines", action: "log" },
                        { name: I18n.tr("java.download"), desc: I18n.tr("tools.javaDownloadDesc"), icon: "coffee", action: "java" },
                        { name: I18n.tr("tools.memOpt"), desc: I18n.tr("tools.memOptDesc"), icon: "refresh", action: "mem" }
                    ]

                    delegate: Rectangle {
                        id: card
                        width: 220; height: 120
                        radius: Theme.shapeLarge
                        color: cardMa.containsMouse ? Theme.surfaceContainerHigh : Theme.surfaceContainer
                        border.color: "transparent"
                        border.width: 1
                        Behavior on color { ColorAnimation { duration: 150 } }

                        MouseArea {
                            id: cardMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (modelData.action === "log")
                                    window.navigateToPage(6, I18n.tr("tools.logSub"))
                                else if (modelData.action === "java")
                                    javaPick.open()
                                else
                                    memOptConfirm.open()
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

            Text {
                id: javaStatusText
                text: ""
                font.pixelSize: 12; color: palette.placeholderText
                visible: text.length > 0
            }

            Item { Layout.fillHeight: true }
        }

        ScrollBar.vertical: OverlayScrollBar {
            followAlways: true
            policy: Theme.alwaysScrollbars ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
            width: 8
        }
    }

    // Java download version picker
    Popup {
        id: javaPick
        parent: root
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        x: Math.round((root.width - width) / 2)
        y: Math.round((root.height - height) / 2)
        width: Math.min(root.width - 64, 420)
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
                text: I18n.tr("tools.javaManage")
                font.pixelSize: 18; font.weight: Font.Bold
                color: palette.text
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Text {
                    text: I18n.tr("java.version")
                    color: palette.placeholderText
                    font.pixelSize: 14
                }
                Item { Layout.fillWidth: true }
                ComboBox {
                    id: javaVerCombo
                    model: ["8", "11", "16", "17", "21", "25"]
                    currentIndex: 3
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Layout.alignment: Qt.AlignRight
                Button {
                    text: I18n.tr("cancel")
                    font.weight: Font.Normal
                    onClicked: javaPick.close()
                }
                Button {
                    text: I18n.tr("java.downloadBtn")
                    highlighted: true
                    font.weight: Font.Normal
                    onClicked: {
                        var ver = parseInt(javaVerCombo.currentText)
                        javaPick.close()
                        kernel.downloadManager.downloadJava(ver, kernel.instanceManager.currentRootDir || kernel.mcDir)
                    }
                }
            }
        }
    }

    // Memory optimization confirm
    Popup {
        id: memOptConfirm
        parent: root
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        x: Math.round((root.width - width) / 2)
        y: Math.round((root.height - height) / 2)
        width: Math.min(root.width - 64, 420)
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
                     enabled: !kernel.memOptimizing
                     onClicked: {
                         memOptConfirm.close()
                         Qt.callLater(function() {
                             kernel.launcherMemoryOptimize()
                         })
                     }
                 }
            }
        }
    }

    Connections {
        target: kernel.downloadManager
        function onJavaDownloaded(majorVersion, javaPath) {
            kernel.javaManager.addBundledJava()
            javaStatusText.text = I18n.tr("java.downloaded").replace("%1", majorVersion)
        }
        function onErrorOccurred(message) {
            if (message.indexOf("Java") >= 0)
                javaStatusText.text = message
        }
    }
}
