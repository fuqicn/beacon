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
import Beacon 1.0

Item {
    id: root

    property var stackView: null
    property var downloadDialog: null
    property string query: ""
    property string sortKey: "relevance"
    property string loader: ""
    property string mcVersion: ""
    property string source: kernel.modSource
    property string type: "resourcepack"   // "resourcepack" | "shader" | "datapack"
    property var results: []

    readonly property int pageSize: 20
    property int pendingOffset: 0
    property bool hasMore: false
    // Filtered tasks: only show downloads for this page's type
    readonly property var filteredTasks: {
        var list = []
        var all = kernel.searchManager.tasks
        for (var i = 0; i < all.length; ++i) {
            if (all[i].type === root.type) list[list.length] = all[i]
        }
        return list
    }

    function formatCount(n) {
        if (n >= 1000000) return (n / 1000000).toFixed(1) + "M"
        if (n >= 1000) return (n / 1000).toFixed(1) + "k"
        return "" + n
    }

    function flatDesc(s) {
        return String(s || "").replace(/\s*\n+\s*/g, " ").replace(/\s+/g, " ").trim()
    }

    function requestPage(offset) {
        root.pendingOffset = offset
        kernel.searchManager.search(root.query, root.sortKey, root.pageSize,
                                     root.mcVersion, root.loader, offset, root.source, root.type)
    }

    function doSearch() {
        root.query = queryField.text.trim()
        root.mcVersion = versionField.text.trim()
        resultList.contentY = 0
        root.hasMore = false
        root.requestPage(0)
    }

    Component.onCompleted: {
        root.source = kernel.modSource
        if (!kernel.searchManager.searching)
            root.requestPage(0)
    }

    Connections {
        target: kernel.searchManager
        function onSearchResultReady(results, type) {
            if (type !== root.type) return   // only accept results for this page's type
            var keepY = resultList.contentY
            var appending = root.pendingOffset > 0
            if (appending)
                root.results = root.results.concat(results)
            else {
                root.results = results
                keepY = 0
            }
            root.hasMore = results.length >= root.pageSize
            if (appending) {
                Qt.callLater(function() {
                    resultList.contentY = keepY
                })
            }
        }
    }

    function maybeLoadMore() {
        if (kernel.searchManager.searching || !root.hasMore) return
        if (resultList.contentHeight <= 0) return
        if (resultList.contentY >= resultList.contentHeight - resultList.height - 80)
            root.requestPage(root.results.length)
    }
    Timer {
        id: bottomTracker
        interval: 400
        repeat: true
        running: root.visible
        onTriggered: root.maybeLoadMore()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 12

        Text {
            text: I18n.tr("searchResource.title")
            font.pixelSize: 22
            font.weight: Font.Bold
            color: palette.text
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            TextField {
                id: queryField
                Layout.fillWidth: true
                placeholderText: I18n.tr("searchResource.placeholder")
                onAccepted: root.doSearch()
            }

            Button {
                text: I18n.tr("searchResource.search")
                font.weight: Font.Normal
                highlighted: true
                enabled: !kernel.searchManager.searching
                onClicked: root.doSearch()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                text: I18n.tr("modSearch.sourceLabel")
                font.pixelSize: 13
                color: palette.placeholderText
            }

            ComboBox {
                id: sourceCombo
                font.weight: Font.Medium
                model: [
                    { text: I18n.tr("modSearch.sourceAll"), key: "all" },
                    { text: I18n.tr("modSearch.sourceModrinth"), key: "modrinth" },
                    { text: I18n.tr("modSearch.sourceCurseForge"), key: "curseforge" }
                ]
                textRole: "text"
                valueRole: "key"
                enabled: !kernel.searchManager.searching
                Component.onCompleted: {
                    for (var i = 0; i < model.length; ++i)
                        if (model[i].key === root.source) { currentIndex = i; break }
                }
                onCurrentIndexChanged: {
                    root.source = model[currentIndex].key
                    root.doSearch()
                }
            }

            Text {
                text: I18n.tr("modSearch.gameVersion")
                font.pixelSize: 13
                color: palette.placeholderText
            }

            TextField {
                id: versionField
                Layout.preferredWidth: 120
                placeholderText: I18n.tr("modSearch.versionPlaceholder")
                font.pixelSize: 12
                onTextEdited: root.mcVersion = versionField.text.trim()
                onEditingFinished: root.doSearch()
                onAccepted: root.doSearch()
            }

            Text {
                text: I18n.tr("modSearch.sort")
                font.pixelSize: 13
                color: palette.placeholderText
            }

            ComboBox {
                id: sortCombo
                font.weight: Font.Medium
                model: [
                    { text: I18n.tr("modSearch.rel"),   key: "relevance" },
                    { text: I18n.tr("modSearch.downloads"), key: "downloads" },
                    { text: I18n.tr("modSearch.newest"),    key: "newest" },
                    { text: I18n.tr("modSearch.updated"),   key: "updated" }
                ]
                textRole: "text"
                valueRole: "key"
                currentIndex: 0
                onCurrentIndexChanged: {
                    root.sortKey = model[currentIndex].key
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

            ScrollBar.vertical: OverlayScrollBar {
                followAlways: true
                policy: Theme.alwaysScrollbars ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                width: 8
            }

            delegate: Rectangle {
                id: modDelegate
                width: resultList.width
                height: implicitHeight
                implicitHeight: 58
                radius: Theme.shapeSmall
                color: ma.containsMouse
                        ? Qt.alpha(Theme.primary, 0.08)
                        : Theme.surfaceContainer

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 12

                    Rectangle {
                        width: 38; height: 38; radius: Theme.shapeSmall
                        color: Qt.alpha(Theme.primary, 0.1)
                        clip: true
                        Image {
                            anchors.fill: parent
                            source: modelData.logoUrl ? "image://modicon/" + Qt.btoa(modelData.logoUrl) : ""
                            asynchronous: true
                            cache: true
                            fillMode: Image.PreserveAspectFit
                            sourceSize.width: 76
                            sourceSize.height: 76
                            mipmap: false
                            visible: modelData.logoUrl !== ""
                        }
                        Text {
                            anchors.centerIn: parent
                            text: modelData.name ? modelData.name.charAt(0).toUpperCase() : "?"
                            font.pixelSize: 16; font.weight: Font.Bold
                            color: Theme.primary
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
                            text: root.flatDesc(modelData.description) || ""
                            font.pixelSize: 11
                            color: palette.placeholderText
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: (modelData.gameVersions || "") +
                                  (modelData.loaders ? "   |   " + modelData.loaders : "")
                            font.pixelSize: 10
                            color: palette.placeholderText
                            elide: Text.ElideRight
                            visible: (modelData.gameVersions || "") !== "" || (modelData.loaders || "") !== ""
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
                            root.stackView.push(Qt.resolvedUrl("SearchResultDetailPage.qml"), {
                                stackView: root.stackView,
                                result: modelData,
                                type: root.type
                            })
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                text: kernel.searchManager.searching ? I18n.tr("modSearch.searching") :
                      (root.results.length === 0 ? I18n.tr("searchResource.noResults") : "")
                font.pixelSize: 13
                color: palette.placeholderText
                RowLayout {
                    anchors.centerIn: parent
                    spacing: 8
                    visible: kernel.searchManager.searching
                    BusyIndicator { running: true; implicitWidth: 18; implicitHeight: 18 }
                    NumberAnimation on opacity {
                        running: kernel.searchManager.searching
                        loops: Animation.Infinite
                        from: 0; to: 1; duration: 600
                    }
                }
                Behavior on opacity { enabled: kernel.searchManager.searching; NumberAnimation { duration: 200 } }
            }
        }

        Item {
            Layout.fillWidth: true
            height: kernel.searchManager.searching && root.pendingOffset > 0 ? 26 : 0
            visible: height > 0
            RowLayout {
                anchors.centerIn: parent
                spacing: 8
                BusyIndicator { running: true; implicitWidth: 18; implicitHeight: 18 }
                Text {
                    text: I18n.tr("search.loadingMore")
                    font.pixelSize: 11
                    color: palette.placeholderText
                }
            }
        }
    }

    function pushDetail(result) {
        if (root.stackView)
            root.stackView.push(Qt.resolvedUrl("SearchResultDetailPage.qml"), {
                stackView: root.stackView,
                result: result,
                type: root.type,
                downloadDialog: root.downloadDialog
            })
    }

    function installResult(result) {
        var sel = kernel.instanceManager.getSelectedInstance()
        var rootDir = kernel.mcDir
        if (sel && sel.rootDir) rootDir = sel.rootDir
        result["type"] = root.type
        kernel.searchManager.installResult(result, rootDir)
    }

    // Progress panel at bottom
    ColumnLayout {
        Layout.fillWidth: true
        visible: root.filteredTasks.length > 0
        spacing: 4

        Text {
            text: I18n.tr("searchResource.installTitle")
            font.pixelSize: 13
            font.weight: Font.Medium
            color: palette.text
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4

            Repeater {
                model: root.filteredTasks
                delegate: Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 36
                    radius: Theme.shapeSmall
                    color: Theme.surfaceContainer

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 8

                        Text {
                            Layout.fillWidth: true
                            text: modelData.name || modelData.fileName
                            font.pixelSize: 12
                            color: palette.text
                            elide: Text.ElideRight
                        }

                        Text {
                            text: {
                                var s = modelData.status
                                if (s === "success")     return I18n.tr("status.success")
                                if (s === "failed")      return I18n.tr("status.failed")
                                if (s === "cancelled")   return I18n.tr("status.cancelled")
                                if (s === "downloading") return I18n.tr("status.downloading").arg(Math.round(modelData.progress * 100))
                                return I18n.tr("status.queued")
                            }
                            font.pixelSize: 11
                            color: modelData.status === "success" ? "#4CAF50"
                                 : modelData.status === "failed" ? "#F44336"
                                 : palette.placeholderText
                        }

                        Button {
                            text: I18n.tr("cancel")
                            font.weight: Font.Normal
                            flat: true
                            visible: modelData.status === "downloading" || modelData.status === "queued"
                            onClicked: kernel.searchManager.cancelTask(modelData.index)
                        }
                        Button {
                            text: I18n.tr("download.retry")
                            font.weight: Font.Normal
                            flat: true
                            visible: modelData.status === "failed" || modelData.status === "cancelled"
                            onClicked: kernel.searchManager.retryTask(modelData.index)
                        }
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.bottom: parent.bottom
                        anchors.right: parent.right
                        height: 3
                        color: Theme.primary
                        opacity: 0.3
                        width: parent.width * modelData.progress
                        visible: modelData.status === "downloading"
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
