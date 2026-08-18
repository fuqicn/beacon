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

Item {
    id: root

property var stackView: null
    property var downloadDialog: null
    property string query: ""
    property string sortKey: "relevance"
    property string loader: ""
    property string mcVersion: ""
    property var results: []

    function formatCount(n) {
        if (n >= 1000000) return (n / 1000000).toFixed(1) + "M"
        if (n >= 1000) return (n / 1000).toFixed(1) + "k"
        return "" + n
    }

    function doSearch() {
        root.query = queryField.text.trim()
        root.mcVersion = versionField.text.trim()
        kernel.modManager.search(root.query, root.sortKey, 20, root.mcVersion, root.loader)
    }

    Component.onCompleted: {
        if (!kernel.modManager.searching)
            kernel.modManager.search("", "relevance", 20)
    }

    Connections {
        target: kernel.modManager
        function onSearchCompleted(results) {
            root.results = results
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 12

        Text {
            text: "模组搜索"
            font.pixelSize: 22
            font.weight: Font.Bold
            color: palette.text
        }

        Text {
            text: "来源: Modrinth"
            font.pixelSize: 11
            color: palette.placeholderText
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            TextField {
                id: queryField
                Layout.fillWidth: true
                placeholderText: "搜索模组..."
                onAccepted: root.doSearch()
            }

            Button {
                text: "搜索"
                font.weight: Font.Normal
                highlighted: true
                enabled: !kernel.modManager.searching
                onClicked: root.doSearch()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                text: "游戏版本:"
                font.pixelSize: 13
                color: palette.placeholderText
            }

            TextField {
                id: versionField
                Layout.preferredWidth: 120
                placeholderText: "如 1.21.1，留空=所有"
                font.pixelSize: 12
                onTextEdited: root.mcVersion = versionField.text.trim()
                onEditingFinished: root.doSearch()
                onAccepted: root.doSearch()
            }

            Text {
                text: "加载器:"
                font.pixelSize: 13
                color: palette.placeholderText
            }

            ComboBox {
                id: loaderCombo
                font.weight: Font.Medium
                model: ListModel {
                    ListElement { text: "所有"; key: "" }
                    ListElement { text: "Fabric"; key: "fabric" }
                    ListElement { text: "Forge"; key: "forge" }
                    ListElement { text: "NeoForge"; key: "neoforge" }
                    ListElement { text: "Quilt"; key: "quilt" }
                }
                textRole: "text"
                currentIndex: 0
                delegate: ItemDelegate {
                    width: parent ? parent.width : 0
                    text: model.text
                    font.weight: Font.Medium
                }
                onCurrentIndexChanged: {
                    root.loader = model.get(currentIndex).key
                    root.doSearch()
                }
            }

            Text {
                text: "排序:"
                font.pixelSize: 13
                color: palette.placeholderText
            }

            ComboBox {
                id: sortCombo
                font.weight: Font.Medium
                model: ListModel {
                    ListElement { text: "相关度"; key: "relevance" }
                    ListElement { text: "下载量"; key: "downloads" }
                    ListElement { text: "关注量"; key: "follows" }
                    ListElement { text: "最新发布"; key: "newest" }
                    ListElement { text: "最近更新"; key: "updated" }
                }
                textRole: "text"
                currentIndex: 0
                delegate: ItemDelegate {
                    width: parent ? parent.width : 0
                    text: model.text
                    font.weight: Font.Medium
                }
                onCurrentIndexChanged: {
                    root.sortKey = model.get(currentIndex).key
                    root.doSearch()
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: palette.mid; opacity: 0.3 }

        ListView {
            id: resultList
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 4
            clip: true
            model: root.results

            ScrollBar.vertical: ScrollBar {
                policy: Theme.alwaysScrollbars ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                width: 8
            }

            delegate: Rectangle {
                id: modDelegate
                width: resultList.width
                height: 60
                radius: Theme.shapeSmall
                color: ma.containsMouse
                       ? Qt.alpha(palette.highlight, 0.08)
                       : Theme.surfaceContainer

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 12

                    Rectangle {
                        width: 38; height: 38; radius: Theme.shapeSmall
                        color: Qt.alpha(palette.highlight, 0.1)
                        clip: true
                        Image {
                            anchors.fill: parent
                            source: modelData.logoUrl ? "image://modicon/" + Qt.btoa(modelData.logoUrl) : ""
                            asynchronous: true
                            fillMode: Image.PreserveAspectFit
                            visible: modelData.logoUrl !== ""
                        }
                        Text {
                            anchors.centerIn: parent
                            text: modelData.name ? modelData.name.charAt(0).toUpperCase() : "?"
                            font.pixelSize: 16; font.weight: Font.Bold
                            color: palette.highlight
                            visible: !(modelData.logoUrl !== "")
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Text {
                            Layout.fillWidth: true
                            text: modelData.name || ""
                            font.pixelSize: 14
                            font.weight: Font.Medium
                            color: palette.text
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.description || ""
                            font.pixelSize: 11
                            color: palette.placeholderText
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.loaders || ""
                            font.pixelSize: 10
                            color: palette.highlight
                            elide: Text.ElideRight
                            visible: (modelData.loaders || "") !== ""
                        }
                    }

                    Text {
                        text: "↓ " + root.formatCount(modelData.downloadCount || 0)
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        color: palette.placeholderText
                    }
                }

                MouseArea {
                    id: ma
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (root.stackView)
root.stackView.push(Qt.resolvedUrl("ModDetailPage.qml"), {
                                stackView: root.stackView,
                                projectId: modelData.id,
                                mcVersion: root.mcVersion,
                                loader: root.loader,
                                downloadDialog: root.downloadDialog
                            })
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                text: kernel.modManager.searching ? "搜索中..." :
                      (root.results.length === 0 ? "没有找到相关模组" : "")
                font.pixelSize: 13
                color: palette.placeholderText
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                Layout.fillWidth: true
                text: "仅支持 Modrinth 模组下载与安装"
                font.pixelSize: 11
                color: palette.placeholderText
            }

            Button {
                text: "打开 CurseForge 官网"
                font.weight: Font.Normal
                flat: true
                onClicked: Qt.openUrlExternally("https://www.curseforge.com/minecraft/mc-mods")
            }
        }
    }
}
