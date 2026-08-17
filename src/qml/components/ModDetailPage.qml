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
    property string projectId: ""
    property string mcVersion: ""
property string loader: ""

property var project: ({})
    property var versions: []
    property var depNames: ({})
    property VersionGroupModel groupedModel: VersionGroupModel {}
    property var selectedVersion: ({})
    property bool loading: false
    property bool projectLoaded: false
    property bool versionsLoading: false
    property int containerHeight: 52

    property var selectedFile: root.selectedVersion

    function formatCount(n) {
        if (n >= 1000000) return (n / 1000000).toFixed(1) + "M"
        if (n >= 1000) return (n / 1000).toFixed(1) + "k"
        return "" + n
    }

    function releaseLabel(t) {
        if (t === "release") return "正式"
        if (t === "beta") return "Beta"
        if (t === "alpha") return "Alpha"
        return t || ""
    }

    function depTypeLabel(t) {
        if (t === "required") return "必需"
        if (t === "optional") return "可选"
        if (t === "incompatible") return "不兼容"
        if (t === "embedded") return "内置"
        return t || ""
}

    function depTypeColor(t) {
        if (t === "required") return palette.highlight
        if (t === "incompatible") return "#F44336"
        return palette.placeholderText
    }

    // PCL-style grouping, collapse/expand and version ordering are implemented
    // in C++ (VersionGroupModel) so the view only deals with display.

    function refreshDeps() {
        var ids = []
        for (var i = 0; i < root.versions.length; i++) {
            var deps = root.versions[i].dependencies || []
            for (var j = 0; j < deps.length; j++) {
                var pid = deps[j].projectId || ""
                if (pid !== "" && ids.indexOf(pid) < 0)
                    ids.push(pid)
            }
        }
        if (ids.length > 0)
            kernel.modManager.getProjects(ids)
    }

function reloadVersions() {
        root.versionsLoading = true
        root.versions = []
        kernel.modManager.getVersions(root.projectId, root.mcVersion, root.loader)
    }

    Component.onCompleted: {
        kernel.modManager.getProject(root.projectId)
        reloadVersions()
    }

    Connections {
        target: kernel.modManager
function onProjectLoaded(project) {
            if (project.id === root.projectId) {
                root.project = project
                root.projectLoaded = true
            }
        }

function onVersionsLoaded(versions) {
            root.versions = versions
            root.groupedModel.setVersions(versions)
            root.containerHeight = root.groupedModel.listHeight
            root.versionsLoading = false
            refreshDeps()
        }
        function onProjectsLoaded(projects) {
            var map = {}
            for (var i = 0; i < projects.length; i++)
                map[projects[i].id] = projects[i].name || projects[i].slug || projects[i].id
            root.depNames = map
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Top bar: back + title
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
                    text: "\u2190  返回"
                    flat: true
                    font.weight: Font.Normal
                    onClicked: {
                        if (root.stackView)
                            root.stackView.pop()
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: root.project.name || root.projectId
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    color: palette.text
                    elide: Text.ElideRight
                }

                Button {
                    text: "在 Modrinth 打开"
                    flat: true
                    font.weight: Font.Normal
                    onClicked: {
                        var url = root.project.websiteUrl
                        if (!url)
                            url = "https://modrinth.com/mod/" + (root.project.slug || root.projectId)
                        Qt.openUrlExternally(url)
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: palette.mid; opacity: 0.3 }

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

                // Project header
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    Rectangle {
                        width: 56; height: 56; radius: Theme.shapeMedium
                        color: Qt.alpha(palette.highlight, 0.1)
                        clip: true
                        Image {
                            anchors.fill: parent
                            source: root.project.logoUrl ? "image://modicon/" + Qt.btoa(root.project.logoUrl) : ""
                            asynchronous: true
                            fillMode: Image.PreserveAspectFit
                            visible: root.project.logoUrl !== ""
                        }
                        Text {
                            anchors.centerIn: parent
                            text: root.project.name ? root.project.name.charAt(0).toUpperCase() : "?"
                            font.pixelSize: 24; font.weight: Font.Bold
                            color: palette.highlight
                            visible: !(root.project.logoUrl !== "")
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            Layout.fillWidth: true
                            text: root.project.name || (root.projectLoaded ? root.projectId : "加载中...")
                            font.pixelSize: 17
                            font.weight: Font.DemiBold
                            color: palette.text
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: (root.project.description || "") + "    ↓ " +
                                  root.formatCount(root.project.downloadCount || 0)
                            font.pixelSize: 12
                            color: palette.placeholderText
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: (root.project.gameVersions || "") +
                                  (root.project.loaders ? "   |   " + root.project.loaders : "")
                            font.pixelSize: 10
                            color: palette.highlight
                            elide: Text.ElideRight
                            visible: (root.project.gameVersions || "") !== "" || (root.project.loaders || "") !== ""
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: palette.mid; opacity: 0.3 }

                // Version / loader filter
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
                        text: root.mcVersion
                        placeholderText: "留空=所有"
                        font.pixelSize: 12
                        onTextEdited: {
                            root.mcVersion = text.trim()
                            root.reloadVersions()
                        }
                        onEditingFinished: root.reloadVersions()
                        onAccepted: root.reloadVersions()
                    }

                    Text {
                        text: "加载器: " + (root.loader || "全部")
                        font.pixelSize: 13
                        color: palette.placeholderText
                    }

                    Item { Layout.fillWidth: true }
                }

                // Version files
                Text {
                    text: "版本文件"
                    font.pixelSize: 13
                    font.weight: Font.Medium
                    color: palette.placeholderText
                }

Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.containerHeight
                    Behavior on Layout.preferredHeight { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
                    radius: Theme.shapeMedium
                    color: Qt.alpha(palette.placeholderText, 0.05)
                    clip: true

                    ListView {
                        id: versionsList
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 2
                        clip: true
                        model: root.groupedModel
                        add: Transition {
                            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 180 }
                            NumberAnimation { property: "height"; from: 0; duration: 180; easing.type: Easing.OutCubic }
                        }
                        remove: Transition {
                            NumberAnimation { property: "opacity"; to: 0; duration: 160 }
                            NumberAnimation { property: "height"; to: 0; duration: 160; easing.type: Easing.InCubic }
                        }
                        displaced: Transition {
                            NumberAnimation { properties: "x,y"; duration: 200; easing.type: Easing.OutQuad }
                        }

                        ScrollBar.vertical: ScrollBar {
                            policy: Theme.alwaysScrollbars ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                            width: 8
                        }

                        delegate: Item {
                            id: vdel
                            width: versionsList.width
                            height: type === "header" ? 26 : 44

                            // Collapsible group header: "Minecraft x.y.z (n)"
Rectangle {
                                anchors.fill: parent
                                visible: type === "header"
                                radius: Theme.shapeSmall
                                color: vhArea.containsMouse ? Qt.alpha(palette.highlight, 0.06) : "transparent"

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.left: parent.left
                                    anchors.leftMargin: 10
                                    text: (root.groupedModel.isCollapsed(primary) ? "\u25b8" : "\u25be") + "  Minecraft " + primary
                                          + "  (" + count + ")"
                                    font.pixelSize: 11
                                    font.weight: Font.DemiBold
                                    color: palette.highlight
                                }

                                MouseArea {
                                    id: vhArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        root.groupedModel.toggleGroup(primary)
                                        root.containerHeight = root.groupedModel.listHeight
                                    }
                                }
                            }

                            // Version row
                            Rectangle {
                                anchors.fill: parent
                                visible: type === "item"
                                radius: Theme.shapeSmall
                                color: ver
                                       ? (root.selectedVersion.id === ver.id
                                          ? Qt.alpha(palette.highlight, 0.14)
                                          : (vma.containsMouse
                                             ? Qt.alpha(palette.placeholderText, 0.10)
                                             : Qt.alpha(palette.placeholderText, 0.06)))
                                       : "transparent"

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
                                            text: ver ? (ver.displayName || ver.fileName) : ""
                                            font.pixelSize: 13
                                            font.weight: ver && root.selectedVersion.id === ver.id ? Font.Medium : Font.Normal
                                            color: palette.text
                                            elide: Text.ElideRight
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            text: ver ? root.releaseLabel(ver.releaseType) +
                                                  (ver.releaseDate ? "   |   " + ver.releaseDate.slice(0, 10) : "") +
                                                  "   |   " + root.formatCount(ver.downloadCount || 0) + " 下载" : ""
                                            font.pixelSize: 10
                                            color: palette.placeholderText
                                            elide: Text.ElideRight
                                        }
                                    }

                                    Text {
                                        text: ver ? ver.fileName : ""
                                        font.pixelSize: 10
                                        color: palette.highlight
                                        elide: Text.ElideLeft
                                        Layout.maximumWidth: 160
                                    }
                                }

                                MouseArea {
                                    id: vma
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        root.selectedVersion = ver
                                        root.openDownloadDialog()
                                    }
                                }
                            }
                        }

                        BusyIndicator {
                            anchors.centerIn: parent
                            visible: root.versionsLoading
                            running: root.versionsLoading
                        }

                        Text {
                            anchors.centerIn: parent
                            visible: !root.versionsLoading && root.versions.length === 0
                            text: "该加载器/版本下暂无可用文件"
                            font.pixelSize: 12
                            color: palette.placeholderText
                        }
                    }
                }

                // Dependencies of selected file
                Text {
                    text: "前置依赖"
                    font.pixelSize: 13
                    font.weight: Font.Medium
                    color: palette.placeholderText
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    visible: (root.selectedFile.dependencies || []).length > 0

                    Repeater {
                        model: root.selectedFile.dependencies || []
                        delegate: Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 40
                            radius: Theme.shapeSmall
                            color: dma.containsMouse
                                   ? Qt.alpha(palette.highlight, 0.08)
                                   : Qt.alpha(palette.placeholderText, 0.05)

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                spacing: 8

                                Text {
                                    Layout.fillWidth: true
                                    text: (root.depNames[modelData.projectId] || "加载中...") +
                                          (modelData.fileName ? "   (" + modelData.fileName + ")" : "")
                                    font.pixelSize: 13
                                    color: palette.text
                                    elide: Text.ElideRight
                                }

                                Text {
                                    text: root.depTypeLabel(modelData.dependencyType)
                                    font.pixelSize: 10
                                    font.weight: Font.DemiBold
                                    color: root.depTypeColor(modelData.dependencyType)
                                }
                            }

                            MouseArea {
                                id: dma
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
onClicked: {
                                    if (root.stackView && modelData.projectId) {
                                        root.stackView.push(Qt.resolvedUrl("ModDetailPage.qml"), {
                                            stackView: root.stackView,
                                            projectId: modelData.projectId,
                                            mcVersion: root.mcVersion,
                                            loader: root.loader,
                                            downloadDialog: root.downloadDialog
                                        })
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: palette.mid; opacity: 0.3 }

                // Install
                Text {
                    id: installStatus
                    text: ""
                    font.pixelSize: 12
                    color: palette.placeholderText
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                    visible: text !== ""
                }

                Item { Layout.fillHeight: true }
            }
        }

        ScrollBar.vertical: ScrollBar { }
    }

function openDownloadDialog() {
        if (!root.downloadDialog)
            return
        var sel = kernel.instanceManager.getSelectedInstance()
        if (sel && sel.rootDir) {
            root.downloadDialog.targetDir = kernel.gameDirFor(sel.rootDir, sel.id)
        } else {
            var rootDir = kernel.instanceManager.currentRootDir || kernel.mcDir
            root.downloadDialog.targetDir = root.mcVersion
                ? kernel.gameDirFor(rootDir, root.mcVersion)
                : rootDir
        }
        root.downloadDialog.file = root.selectedFile
        root.downloadDialog.loading = false
        root.downloadDialog.open()
    }

    Connections {
        target: root.downloadDialog
        function onInstalled(message) {
            installStatus.text = message
        }
    }
}
