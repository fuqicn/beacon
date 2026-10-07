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
    property var result: ({})
    property string type: "resourcepack"   // resourcepack | shader | datapack

    // Grouped version list: { type: "header", primary, count } + { type: "item", ver }
    property var versionGroups: []
    property bool versionGroupsCollapsed: true
    property bool hasVersionData: false

    // File list fetched via mc_search_get_files()
    property var files: []
    property var selectedFile: ({})
    property bool loading: true

    function formatCount(n) {
        if (n >= 1000000) return (n / 1000000).toFixed(1) + "M"
        if (n >= 1000) return (n / 1000).toFixed(1) + "k"
        return "" + n
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
        var releases = raw.filter(function(v){return /^\d+\.\d+(\.\d+)*$/.test(v)})
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
        return (loaderPart + (verPart ? "   " + verPart : "")).trim()
    }

    function flatDesc(s) {
        return String(s || "").replace(/\s*\n+\s*/g, " ").replace(/\s+/g, " ").trim()
    }

    function isCurseForgeUrl(url) {
        if (!url) return false
        var host = url.split("//")[1] ? url.split("//")[1].split("/")[0].toLowerCase() : ""
        return host.indexOf("curseforge") >= 0 || host.indexOf("forgecdn") >= 0
    }

    function buildVersionGroups() {
        var raw = (root.result.gameVersions || "").split(",").map(function(s){return s.trim()}).filter(Boolean)
        root.hasVersionData = raw.length > 0
        var releases = raw.filter(function(v){return /^\d+\.\d+(\.\d+)*$/.test(v)})
        releases.sort(cmpVerDesc)
        var groups = {}, order = []
        for (var i = 0; i < releases.length; ++i) {
            var v = releases[i], parts = v.split(".")
            var key = parts[0] + "." + (parts[1]||"0")
            if (!groups[key]) { groups[key] = []; order.push(key) }
            groups[key].push(v)
        }
        // Clear then repopulate to force ListView model refresh
        root.versionGroups = []
        for (var g = 0; g < order.length; ++g) {
            var gm = order[g], vers = groups[gm]
            root.versionGroups[root.versionGroups.length] = { type: "header", primary: "Minecraft " + gm, count: vers.length }
            for (var j = 0; j < vers.length; ++j)
                root.versionGroups[root.versionGroups.length] = { type: "item", ver: vers[j] }
        }
    }

    Component.onCompleted: {
        buildVersionGroups()
        kernel.searchManager.getFiles(root.result.id, root.type)
    }

    Connections {
        target: kernel.searchManager
        function onFilesLoaded(files) {
            root.files = files
            root.loading = false
            // Auto-select primary release file
            for (var i = 0; i < files.length; ++i) {
                if (files[i].isPrimary && files[i].versionType === "release") {
                    root.selectedFile = files[i]; break
                }
            }
            if (!root.selectedFile.id)
                root.selectedFile = (files.length > 0) ? files[0] : {}
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── Top bar ───────────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            height: 48
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 16
                spacing: 8

                Button {
                    text: "\u2190  " + I18n.tr("back")
                    flat: true
                    font.weight: Font.Normal
                    onClicked: { if (root.stackView) root.stackView.pop() }
                }

                Text {
                    Layout.fillWidth: true
                    text: root.result.name || root.result.id || ""
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    color: palette.text
                    elide: Text.ElideRight
                }

                Button {
                    text: root.result.source === "CurseForge"
                          ? I18n.tr("modDetail.openInCurseForge")
                          : I18n.tr("modDetail.openInModrinth")
                    flat: true
                    font.weight: Font.Normal
                    onClicked: {
                        var url = root.result.websiteUrl
                        if (!url) {
                            if (root.result.source === "CurseForge")
                                url = "https://www.curseforge.com/minecraft/projects/" + (root.result.slug || root.result.id)
                            else
                                url = "https://modrinth.com/" + (root.result.projectType || "mod") + "/" + (root.result.slug || root.result.id)
                        }
                        Qt.openUrlExternally(url)
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: palette.mid; opacity: 0.3 }

        // ── Scrollable content ────────────────────────────────────────────────
        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentHeight: content.implicitHeight
            flickableDirection: Flickable.VerticalFlick

            ColumnLayout {
                id: content
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 16
                spacing: 12

                // Header: icon + info (same as ModDetailPage)
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    Rectangle {
                        width: 56; height: 56; radius: Theme.shapeMedium
                        color: Qt.alpha(Theme.primary, 0.1)
                        clip: true
                        Image {
                            anchors.fill: parent
                            source: root.result.logoUrl
                                    ? (root.isCurseForgeUrl(root.result.logoUrl)
                                       ? root.result.logoUrl
                                       : "image://modicon/" + Qt.btoa(root.result.logoUrl))
                                    : ""
                            asynchronous: true
                            cache: true
                            fillMode: Image.PreserveAspectFit
                            sourceSize.width: 96
                            sourceSize.height: 96
                            mipmap: false
                            visible: root.result.logoUrl !== ""
                        }
                        Text {
                            anchors.centerIn: parent
                            text: root.result.name ? root.result.name.charAt(0).toUpperCase() : "?"
                            font.pixelSize: 24; font.weight: Font.Bold
                            color: Theme.primary
                            visible: !(root.result.logoUrl !== "")
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            Layout.fillWidth: true
                            text: root.result.name || (root.result.id || "")
                            font.pixelSize: 17
                            font.weight: Font.DemiBold
                            color: palette.text
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: root.flatDesc(root.result.description) || ""
                            font.pixelSize: 12
                            color: palette.placeholderText
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            Layout.fillWidth: true
                            text: root.supportLine(root.result.loaders, root.result.gameVersions)
                            font.pixelSize: 11
                            color: Theme.primary
                            elide: Text.ElideRight
                            visible: (root.result.gameVersions || "") !== "" || (root.result.loaders || "") !== ""
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: palette.mid; opacity: 0.3 }

                // ── Version support list ─────────────────────────────────────────
                Text {
                    text: I18n.tr("searchResource.supportedVersions")
                    font.pixelSize: 13
                    color: palette.placeholderText
                    visible: root.hasVersionData
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.hasVersionData
                            ? Math.min(root.versionGroups.length * 26, 200)
                            : 0
                    Behavior on Layout.preferredHeight { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
                    radius: Theme.shapeMedium
                    color: Theme.surfaceContainer
                    clip: true
                    visible: root.hasVersionData

                    ListView {
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 2
                        clip: true
                        model: root.versionGroups

                        ScrollBar.vertical: OverlayScrollBar {
                            followAlways: true
                            policy: Theme.alwaysScrollbars ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                            width: 8
                        }

                        delegate: Item {
                            width: ListView.view.width
                            height: modelData.type === "header" ? 26 : 26

                            // Group header
                            Rectangle {
                                anchors.fill: parent
                                visible: modelData.type === "header"
                                radius: Theme.shapeSmall
                                color: vhArea.containsMouse ? Qt.alpha(Theme.primary, 0.06) : "transparent"

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.left: parent.left
                                    anchors.leftMargin: 10
                                    text: (root.versionGroupsCollapsed ? "\u25b8" : "\u25be") + "  " + modelData.primary
                                          + "  (" + modelData.count + ")"
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                    color: Theme.primary
                                }

                                MouseArea {
                                    id: vhArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.versionGroupsCollapsed = !root.versionGroupsCollapsed
                                }
                            }

                            // Version item
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                anchors.leftMargin: modelData.type === "item" ? 24 : 10
                                text: modelData.type === "item" ? modelData.ver : ""
                                font.pixelSize: 12
                                color: palette.text
                                elide: Text.ElideRight
                                visible: modelData.type === "item" && !root.versionGroupsCollapsed
                            }
                        }
                    }
                }

                // ── File list (with download URLs) ───────────────────────────────
                Text {
                    text: I18n.tr("modDetail.versionFiles")
                    font.pixelSize: 13
                    font.weight: Font.Medium
                    color: palette.placeholderText
                    visible: !root.loading && root.files.length > 0
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.loading ? 0 : Math.min(root.files.length * 44 + 12, 200)
                    Behavior on Layout.preferredHeight { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
                    radius: Theme.shapeMedium
                    color: Theme.surfaceContainer
                    clip: true
                    visible: !root.loading && root.files.length > 0

                    ListView {
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 2
                        clip: true
                        model: root.files

                        ScrollBar.vertical: OverlayScrollBar {
                            followAlways: true
                            policy: Theme.alwaysScrollbars ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                            width: 8
                        }

                        delegate: Rectangle {
                            width: ListView.view.width
                            height: 44
                            radius: Theme.shapeSmall
                            color: root.selectedFile.id === modelData.id
                                    ? Qt.alpha(Theme.primary, 0.14)
                                    : (dma.containsMouse
                                       ? Qt.alpha(palette.placeholderText, 0.10)
                                       : "transparent")

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                spacing: 8

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.fileName || ""
                                        font.pixelSize: 13
                                        font.weight: root.selectedFile.id === modelData.id ? Font.Medium : Font.Normal
                                        color: palette.text
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: (modelData.versionType === "release" ? I18n.tr("type.release")
                                             : modelData.versionType === "beta" ? "Beta"
                                             : modelData.versionType === "alpha" ? "Alpha"
                                             : modelData.versionType || "")
                                              + (modelData.datePublished ? "   |   " + modelData.datePublished.slice(0, 10) : "")
                                        font.pixelSize: 10
                                        color: palette.placeholderText
                                        elide: Text.ElideRight
                                    }
                                }

                                Text {
                                    text: modelData.isPrimary ? "[主]" : ""
                                    font.pixelSize: 10
                                    color: Theme.primary
                                    elide: Text.ElideLeft
                                    Layout.maximumWidth: 30
                                }
                            }

                            MouseArea {
                                id: dma
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.selectedFile = modelData
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }
    }
}
