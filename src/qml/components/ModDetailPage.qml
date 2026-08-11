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

Item {
    id: root

    property var stackView: null
    property string projectId: ""
    property string mcVersion: ""
    property string loader: ""

    property var project: ({})
    property var versions: []
    property var depNames: ({})
    property int selectedIndex: -1
    property bool loading: false

    property var selectedFile: root.selectedIndex >= 0 && root.selectedIndex < root.versions.length
                               ? root.versions[root.selectedIndex] : ({})

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
        loading = true
        root.versions = []
        kernel.modManager.getVersions(root.projectId, root.mcVersion, root.loader)
    }

    Component.onCompleted: {
        loading = true
        kernel.modManager.getProject(root.projectId)
        reloadVersions()
    }

    Connections {
        target: kernel.modManager
        function onProjectLoaded(project) {
            if (project.id === root.projectId) {
                root.project = project
                loading = false
            }
        }
        function onVersionsLoaded(versions) {
            root.versions = versions
            root.selectedIndex = versions.length > 0 ? 0 : -1
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
                            text: root.project.name || (root.loading ? "加载中..." : root.projectId)
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
                    Layout.preferredHeight: {
                        var n = root.versions.length
                        var h = n * 46 + 12
                        return Math.max(52, Math.min(h, 220))
                    }
                    radius: Theme.shapeMedium
                    color: Qt.alpha(palette.placeholderText, 0.05)
                    clip: true

                    ListView {
                        id: versionsList
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 2
                        clip: true
                        model: root.versions

                        ScrollBar.vertical: ScrollBar {
                            policy: Theme.alwaysScrollbars ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                            width: 8
                        }

                        delegate: Rectangle {
                            id: vdel
                            width: versionsList.width
                            height: 44
                            radius: Theme.shapeSmall
                            color: index === root.selectedIndex
                                   ? Qt.alpha(palette.highlight, 0.12)
                                   : (vma.containsMouse ? Qt.alpha(palette.highlight, 0.08) : "transparent")

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
                                        text: modelData.displayName || modelData.fileName || ""
                                        font.pixelSize: 13
                                        font.weight: index === root.selectedIndex ? Font.Medium : Font.Normal
                                        color: palette.text
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: root.releaseLabel(modelData.releaseType) +
                                              (modelData.releaseDate ? "   |   " + modelData.releaseDate.slice(0, 10) : "") +
                                              "   |   " + root.formatCount(modelData.downloadCount || 0) + " 下载"
                                        font.pixelSize: 10
                                        color: palette.placeholderText
                                        elide: Text.ElideRight
                                    }
                                }

                                Text {
                                    text: modelData.fileName || ""
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
                                    root.selectedIndex = index
                                    root.openDownloadDialog()
                                }
                            }
                        }

                        Text {
                            anchors.centerIn: parent
                            text: root.versions.length === 0 ? "该加载器/版本下暂无可用文件" : ""
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
                                            loader: root.loader
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
        var sel = kernel.instanceManager.getSelectedInstance()
        if (sel && sel.rootDir) {
            downloadDialog.targetDir = kernel.gameDirFor(sel.rootDir, sel.id)
        } else {
            var rootDir = kernel.instanceManager.currentRootDir || kernel.mcDir
            downloadDialog.targetDir = root.mcVersion
                ? kernel.gameDirFor(rootDir, root.mcVersion)
                : rootDir
        }
        downloadDialog.open()
    }

    FolderDialog {
        id: downloadDirDialog
        currentFolder: "file:///" + (downloadDialog.targetDir || kernel.mcDir)
        onAccepted: {
            if (selectedFolder)
                downloadDialog.targetDir = selectedFolder.toString().replace(/^file:\/\//, "")
        }
    }

    Popup {
        id: downloadDialog

        property string targetDir: ""
        readonly property bool fileReady: !root.loading && root.selectedFile
                                          && root.selectedFile.fileName
                                          && root.selectedFile.downloadUrl

        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        // Anchor to the full-window content so x/y are window-relative and
        // independent of this page's (possibly mid-transition) scene transform.
        parent: root.Window.window ? root.Window.window.contentItem : root
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: Math.min(Math.max(parent.width - 64, 320), 460)
        padding: 24

        // Stay invisible until the popup has a real height (first layout done),
        // so it never renders a frame at a wrong spot.
        property bool placed: false
        opacity: placed ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 120 } }

        onOpened: {
            dirLabel.text = downloadDialog.targetDir
            downloadDialog.placed = (downloadDialog.height > 0)
        }

        onHeightChanged: {
            if (downloadDialog.opened && downloadDialog.height > 0)
                downloadDialog.placed = true
        }

        background: Rectangle {
            radius: Theme.shapeLarge
            color: palette.window
            border.color: palette.mid
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: 16

            // Loading state (versions still being fetched)
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 12
                visible: !downloadDialog.fileReady

                BusyIndicator {
                    Layout.alignment: Qt.AlignHCenter
                    running: true
                    implicitWidth: 36
                    implicitHeight: 36
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: "正在加载版本信息..."
                    font.pixelSize: 12
                    color: palette.placeholderText
                }
            }

            // Ready state
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 16
                visible: downloadDialog.fileReady

                Text {
                    text: "下载 " + (root.selectedFile.displayName || root.selectedFile.fileName || "模组")
                    font.pixelSize: 18
                    font.weight: Font.Bold
                    color: palette.text
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                Text {
                    text: root.selectedFile.fileName || ""
                    font.pixelSize: 12
                    color: palette.placeholderText
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    visible: text !== ""
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        text: "下载目录"
                        font.pixelSize: 13
                        color: palette.placeholderText
                    }

                    Text {
                        id: dirLabel
                        Layout.fillWidth: true
                        text: downloadDialog.targetDir
                        font.pixelSize: 12
                        color: palette.text
                        elide: Text.ElideMiddle
                    }

                    Button {
                        text: "选择..."
                        font.weight: Font.Normal
                        onClicked: downloadDirDialog.open()
                    }
                }

                Text {
                    text: "模组将安装到所选目录的 mods 文件夹"
                    font.pixelSize: 11
                    color: palette.placeholderText
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Layout.alignment: Qt.AlignRight

                    Item { Layout.fillWidth: true }

                    Button {
                        text: "取消"
                        font.weight: Font.Normal
                        onClicked: downloadDialog.close()
                    }

                    Button {
                        text: "下载"
                        font.weight: Font.Normal
                        highlighted: true
                        onClicked: {
                            if (downloadDialog.targetDir.length > 0) {
                                kernel.modManager.installMod(root.selectedFile, downloadDialog.targetDir)
                                installStatus.text = "已加入下载队列，可在下方下载面板查看进度"
                            }
                            downloadDialog.close()
                        }
                    }
                }
            }
        }
    }
}
