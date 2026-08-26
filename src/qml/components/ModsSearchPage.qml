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

    // Pagination: Modrinth offset-based, auto-load on near-bottom scroll.
    readonly property int pageSize: 20
    property int pendingOffset: 0
    property bool hasMore: false

    function formatCount(n) {
        if (n >= 1000000) return (n / 1000000).toFixed(1) + "M"
        if (n >= 1000) return (n / 1000).toFixed(1) + "k"
        return "" + n
    }
    function flatDesc(s) {
        return String(s || "").replace(/\s*\n+\s*/g, " ").replace(/\s+/g, " ").trim()
    }
    function cap(s) { return s ? s.charAt(0).toUpperCase() + s.slice(1) : s }
    function cmpVerDesc(a, b) {
        var pa = (a||"").split(".").map(Number), pb = (b||"").split(".").map(Number)
        for (var i=0; i<Math.max(pa.length,pb.length); i++) {
            var x = pa[i]||0, y = pb[i]||0
            if (x < y) return 1; if (x > y) return -1
        }
        return 0
    }
    function supportLine(loadersCsv, versionsCsv) {
        var lds = (loadersCsv||"").split(",").map(function(s){return s.trim()}).filter(Boolean)
        var loaderPart = ""
        if (lds.length === 1) loaderPart = cap(lds[0])
        else if (lds.length > 1) loaderPart = lds.map(cap).join(" / ")
        var raw = (versionsCsv||"").split(",").map(function(s){return s.trim()}).filter(Boolean)
        var releases = raw.filter(function(v){return /^\d+\.\d+(\.\d+)?$/.test(v)})
        releases.sort(cmpVerDesc)
        var hasSnap = raw.length !== releases.length
        var verPart = ""
        if (releases.length === 0) verPart = hasSnap ? "仅快照版本" : ""
        else if (releases.length === 1) verPart = releases[0]
        else {
            var first = releases[0], last = releases[releases.length-1]
            if (releases.length >= 4 && !first.match(/\d+\.\d+\.\d+$/))
                verPart = first + "~" + last
            else
                verPart = releases.slice(0, 4).join(", ") + (releases.length > 4 ? " ..." : "")
        }
        return (loaderPart + (verPart ? " " + verPart : "")).trim()
    }

    function requestPage(offset) {
        root.pendingOffset = offset
        kernel.modManager.search(root.query, root.sortKey, root.pageSize,
                                 root.mcVersion, root.loader, offset)
    }

    function doSearch() {
        root.query = queryField.text.trim()
        root.mcVersion = versionField.text.trim()
        resultList.contentY = 0
        root.hasMore = false
        root.requestPage(0)
    }

    Component.onCompleted: {
        if (!kernel.modManager.searching)
            root.requestPage(0)
    }

    Connections {
        target: kernel.modManager
        function onSearchCompleted(results) {
            // Appending must not yank the view back to the top: remember the
            // offset and restore it after the model swap (same item heights,
            // so contentY stays valid).
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
                    // Drop stale JS wrappers / trimmed caches after a page load
                    kernel.qmlCollectGarbage()
                })
            }
        }
    }

    // Auto-load next page when scrolled near the bottom.
    function maybeLoadMore() {
        if (kernel.modManager.searching || !root.hasMore) return
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
            text: I18n.tr("modSearch.title")
            font.pixelSize: 22
            font.weight: Font.Bold
            color: palette.text
        }

        Text {
            text: I18n.tr("modSearch.source")
            font.pixelSize: 11
            color: palette.placeholderText
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            TextField {
                id: queryField
                Layout.fillWidth: true
                placeholderText: I18n.tr("modSearch.placeholder")
                onAccepted: root.doSearch()
            }

            Button {
                text: I18n.tr("modSearch.search")
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
                text: I18n.tr("modSearch.loader")
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
                text: I18n.tr("modSearch.sort")
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
                            fillMode: Image.PreserveAspectFit
                            sourceSize.width: 76
                            sourceSize.height: 76
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
                            text: root.supportLine(modelData.loaders, modelData.gameVersions)
                            font.pixelSize: 10
                            color: palette.placeholderText
                            elide: Text.ElideRight
                            visible: root.supportLine(modelData.loaders, modelData.gameVersions) !== ""
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
                text: kernel.modManager.searching ? I18n.tr("modSearch.searching") :
                      (root.results.length === 0 ? I18n.tr("modSearch.noResults") : "")
                font.pixelSize: 13
                color: palette.placeholderText
            }
        }

        Item {
            Layout.fillWidth: true
            height: kernel.modManager.searching && root.pendingOffset > 0 ? 26 : 0
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

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                Layout.fillWidth: true
                text: I18n.tr("modSearch.supportModrinth")
                font.pixelSize: 11
                color: palette.placeholderText
            }

            Button {
                text: I18n.tr("modSearch.curseforge")
                font.weight: Font.Normal
                flat: true
                onClicked: Qt.openUrlExternally("https://www.curseforge.com/minecraft/mc-mods")
            }
        }
    }
}
