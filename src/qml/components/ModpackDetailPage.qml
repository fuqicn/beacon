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
    property var installDialog: null
    property string projectId: ""
    property string mcVersion: ""
property string loader: ""

property var project: ({})
    property var versions: []
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
        if (t === "release") return I18n.tr("type.release")
        if (t === "beta") return "Beta"
        if (t === "alpha") return "Alpha"
        return t || ""
    }

// PCL-style grouping, collapse/expand and version ordering are implemented
    // in C++ (VersionGroupModel) so the view only deals with display.

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
                    text: "\u2190  " + I18n.tr("modpackDetail.back")
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
                    text: I18n.tr("modpackDetail.openInModrinth")
                    flat: true
                    font.weight: Font.Normal
                    onClicked: {
                        var url = root.project.websiteUrl
                        if (!url)
                            url = "https://modrinth.com/modpack/" + (root.project.slug || root.projectId)
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
                        color: Qt.alpha(Theme.primary, 0.1)
                        clip: true
                        Image {
                            anchors.fill: parent
                            source: root.project.logoUrl ? "image://modicon/" + Qt.btoa(root.project.logoUrl) : ""
                            asynchronous: true
                            fillMode: Image.PreserveAspectFit
                            sourceSize.width: 96
                            sourceSize.height: 96
                        }
                        Text {
                            anchors.centerIn: parent
                            text: root.project.name ? root.project.name.charAt(0).toUpperCase() : "?"
                            font.pixelSize: 24; font.weight: Font.Bold
                            color: Theme.primary
                            visible: !(root.project.logoUrl !== "")
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            Layout.fillWidth: true
                            text: root.project.name || (root.projectLoaded ? root.projectId : I18n.tr("modpackDetail.loading"))
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
                            color: Theme.primary
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
                        text: I18n.tr("modpackDetail.gameVersion")
                        font.pixelSize: 13
                        color: palette.placeholderText
                    }

                    TextField {
                        id: versionField
                        Layout.preferredWidth: 120
                        text: root.mcVersion
                        placeholderText: I18n.tr("modpackDetail.emptyAll")
                        font.pixelSize: 12
                        onTextEdited: {
                            root.mcVersion = text.trim()
                            root.reloadVersions()
                        }
                        onEditingFinished: root.reloadVersions()
                        onAccepted: root.reloadVersions()
                    }

                    Text {
                        text: I18n.tr("modpackDetail.loaderPrefix") + (root.loader || I18n.tr("modpackDetail.allLoaders"))
                        font.pixelSize: 13
                        color: palette.placeholderText
                    }

                    Item { Layout.fillWidth: true }
                }

                // Version files
                Text {
                    text: I18n.tr("modpackDetail.versionFiles")
                    font.pixelSize: 13
                    font.weight: Font.Medium
                    color: palette.placeholderText
                }

Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.containerHeight
                    Behavior on Layout.preferredHeight { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
                    radius: Theme.shapeMedium
                    color: Theme.surfaceContainer
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

                        ScrollBar.vertical: OverlayScrollBar {
                            followAlways: true
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
                                color: vhArea.containsMouse ? Qt.alpha(Theme.primary, 0.06) : "transparent"

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.left: parent.left
                                    anchors.leftMargin: 10
                                    text: (root.groupedModel.isCollapsed(primary) ? "\u25b8" : "\u25be") + "  Minecraft " + primary
                                          + "  (" + count + ")"
                                    font.pixelSize: 11
                                    font.weight: Font.DemiBold
                                    color: Theme.primary
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
                                          ? Qt.alpha(Theme.primary, 0.14)
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
                                                  "   |   " + root.formatCount(ver.downloadCount || 0) + I18n.tr("modpackDetail.downloads") : ""
                                            font.pixelSize: 10
                                            color: palette.placeholderText
                                            elide: Text.ElideRight
                                        }
                                    }

                                    Text {
                                        text: ver ? ver.fileName : ""
                                        font.pixelSize: 10
                                        color: Theme.primary
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
                                        root.openInstallDialog()
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
                            text: I18n.tr("modpackDetail.noVersions")
                            font.pixelSize: 12
                            color: palette.placeholderText
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: palette.mid; opacity: 0.3 }

                // Install status
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

        ScrollBar.vertical: OverlayScrollBar { }
    }

function openInstallDialog() {
        if (!root.installDialog)
            return
        root.installDialog.file = root.selectedFile
        root.installDialog.loading = false
        root.installDialog.projectName = root.project.name
        root.installDialog.logoUrl = root.project.logoUrl || ""
        root.installDialog.targetDir = kernel.instanceManager.currentRootDir || kernel.mcDir
        root.installDialog.open()
    }

    Connections {
        target: root.installDialog
        function onInstalled(message) {
            installStatus.text = message
        }
    }
}