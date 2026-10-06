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

    function formatCount(n) {
        if (n >= 1000000) return (n / 1000000).toFixed(1) + "M"
        if (n >= 1000) return (n / 1000).toFixed(1) + "k"
        return "" + n
    }

    function flatDesc(s) {
        return String(s || "").replace(/\s*\n+\s*/g, " ").replace(/\s+/g, " ").trim()
    }

    function doInstall() {
        var sel = kernel.instanceManager.getSelectedInstance()
        var rootDir = sel && sel.rootDir ? sel.rootDir : kernel.mcDir
        var verId   = sel ? sel.id : ""
        var gameDir = kernel.gameDirFor(rootDir, verId)
        var r = Object.assign({}, root.result)
        r["type"] = root.type
        kernel.searchManager.installResult(r, gameDir)
    }

    Component.onCompleted: {
        // Parse gameVersions and build grouped list
        var raw = (root.result.gameVersions || "").split(",").map(function(s){return s.trim()}).filter(Boolean)
        var releases = raw.filter(function(v){return /^\d+\.\d+(\.\d+)*$/.test(v)})
        releases.sort(function(a,b){
            var pa = a.split(".").map(Number), pb = b.split(".").map(Number)
            for (var i=0; i<Math.max(pa.length,pb.length); i++) {
                var x = pa[i]||0, y = pb[i]||0
                if (x < y) return 1; if (x > y) return -1
            }
            return 0
        })
        var groups = {}, order = []
        for (var i = 0; i < releases.length; ++i) {
            var v = releases[i], parts = v.split(".")
            var key = parts[0] + "." + (parts[1]||"0")
            if (!groups[key]) { groups[key] = []; order.push(key) }
            groups[key].push(v)
        }
        var items = []
        for (var g = 0; g < order.length; ++g) {
            var gm = order[g], vers = groups[gm]
            items[items.length] = { type: "header", primary: "Minecraft " + gm, count: vers.length }
            for (var j = 0; j < vers.length; ++j)
                items[items.length] = { type: "item", ver: vers[j] }
        }
        root.versionGroups = items
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

        // ── Version support list ──────────────────────────────────────────────
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

                Text {
                    text: I18n.tr("searchResource.supportedVersions")
                    font.pixelSize: 13
                    color: palette.placeholderText
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(
                        root.versionGroups.reduce(function(s, item){
                            return s + (item.type === "header" ? 26 : 26)
                        }, 0),
                        200)
                    radius: Theme.shapeMedium
                    color: Theme.surfaceContainer
                    clip: true

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
                            height: type === "header" ? 26 : 26

                            // Group header
                            Rectangle {
                                anchors.fill: parent
                                visible: type === "header"
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
                                anchors.leftMargin: type === "item" ? 24 : 10
                                text: type === "item" ? modelData.ver : ""
                                font.pixelSize: 12
                                color: palette.text
                                elide: Text.ElideRight
                                visible: type === "item" && !root.versionGroupsCollapsed
                            }
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        text: I18n.tr("modDetail.noVersions")
                        font.pixelSize: 12
                        color: palette.placeholderText
                        visible: root.versionGroups.length === 0
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }
    }
}
