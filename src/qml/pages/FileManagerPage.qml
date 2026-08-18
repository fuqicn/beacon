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

    property string currentPath: kernel.launchManager.gameDir || kernel.mcDir
    property string homePath: currentPath
    property var entries: []

    function refresh() {
        entries = kernel.listDir(currentPath)
        pathField.text = currentPath
    }

    Component.onCompleted: {
        currentPath = String(currentPath).replace(/\\/g, "/")
        root.refresh()
    }

    function goTo(path) {
        var p = String(path).trim()
        if (p.indexOf("file:") === 0)
            p = p.replace(/^file:\/\//, "")
        p = p.replace(/\\/g, "/")
        currentPath = p
        refresh()
    }

    function formatSize(bytes) {
        if (bytes >= 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(1) + " MB"
        if (bytes >= 1024) return (bytes / 1024).toFixed(1) + " KB"
        return bytes + " B"
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Button {
                text: "\u2190  " + I18n.tr("back")
                flat: true
                font.weight: Font.Normal
                onClicked: window.navigateTo(3)
            }

            Text {
                text: I18n.tr("file.title")
                font.pixelSize: 20
                font.weight: Font.Bold
                color: palette.text
                Layout.fillWidth: true
            }

            Text {
                text: root.entries.length + " " + I18n.tr("file.items")
                font.pixelSize: 12
                color: palette.placeholderText
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Button {
                text: "\u2191"
                font.weight: Font.Normal
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: I18n.tr("file.up")
                enabled: root.currentPath !== root.homePath
                onClicked: {
                    var parts = root.currentPath.split("/")
                    parts.pop()
                    root.goTo(parts.join("/") || root.homePath)
                }
            }

            Button {
                text: "\u2302"
                font.weight: Font.Normal
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: I18n.tr("file.home")
                onClicked: root.goTo(root.homePath)
            }

            TextField {
                id: pathField
                Layout.fillWidth: true
                font.pixelSize: 13
                selectByMouse: true
                onAccepted: root.goTo(text)
            }

            Button {
                text: I18n.tr("file.open")
                font.weight: Font.Normal
                onClicked: root.goTo(pathField.text)
            }

            Button {
                text: I18n.tr("refresh")
                font.weight: Font.Normal
                highlighted: true
                onClicked: root.refresh()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.shapeMedium
            color: Theme.surfaceContainer
            clip: true

            ListView {
                id: listView
                anchors.fill: parent
                anchors.margins: 6
                clip: true
                model: root.entries
                spacing: 2

                ScrollBar.vertical: ScrollBar {
                    policy: Theme.alwaysScrollbars ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                    width: 8
                }

                delegate: Rectangle {
                    width: listView.width
                    height: 40
                    radius: Theme.shapeSmall
                    color: fileHover.containsMouse ? Qt.alpha(palette.highlight, 0.1) : "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 12

                        AppIcon {
                            iconName: modelData.isDir ? "folder" : "file-lines"
                            iconSize: 18
                            Layout.preferredWidth: 20
                        }

                        Text {
                            text: modelData.name
                            font.pixelSize: 13
                            color: palette.text
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                            font.weight: modelData.isDir ? Font.Medium : Font.Normal
                        }

                        Text {
                            text: modelData.isDir ? "" : root.formatSize(modelData.size)
                            font.pixelSize: 11
                            color: palette.placeholderText
                        }

                        Text {
                            text: modelData.modified
                            font.pixelSize: 11
                            color: palette.placeholderText
                        }

                        Rectangle {
                            width: 24; height: 24; radius: Theme.shapeSmall
                            visible: fileHover.containsMouse
                            color: Qt.alpha(palette.highlight, 0.15)
                            AppIcon {
                                anchors.centerIn: parent
                                iconName: "magnifying-glass"
                                iconSize: 12
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: kernel.revealInExplorer(modelData.path)
                            }
                            ToolTip.visible: parent.visible && fileHover.containsMouse
                            ToolTip.delay: 400
                            ToolTip.text: "在资源管理器中显示"
                        }
                    }

                    MouseArea {
                        id: fileHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (modelData.isDir)
                                root.goTo(modelData.path)
                            else
                                Qt.openUrlExternally("file:///" + modelData.path.replace(/\\/g, "/"))
                        }
                    }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            text: I18n.tr("file.hint")
            font.pixelSize: 11
            color: palette.placeholderText
        }
    }
}
