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

    property var categories: []
    property var filteredVersions: []
    property int currentCategory: 0
    property bool loading: false

    property int modeIndex: 0

function selectCategory(index) {
        currentCategory = index
        if (index >= 0 && index < root.categories.length) {
            filteredVersions = root.categories[index].versions || []
        } else {
            filteredVersions = []
        }
    }

    onCurrentCategoryChanged: {
        if (listContainer && Theme.animationsEnabled) {
            listContainer.scale = 0.96
            catSwitchAnim.restart()
        }
    }

    NumberAnimation {
        id: catSwitchAnim
        target: listContainer
        property: "scale"
        to: 1
        duration: 240
        easing.type: Easing.OutCubic
    }

    Connections {
        target: kernel.versionManager
        function onManifestReady() {
            loading = false
            root.categories = kernel.versionManager.classifyVersions()
            categoryTabs.model = root.categories
            selectCategory(0)
        }
    }

    Component.onCompleted: {
        loading = true
        if (kernel.versionManager.versionCount > 0) {
            root.categories = kernel.versionManager.classifyVersions()
            categoryTabs.model = root.categories
            selectCategory(0)
            loading = false
        } else {
            kernel.versionManager.fetchManifest()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 12

        Text {
            text: "下载"
            font.pixelSize: 22
            font.weight: Font.Bold
            color: palette.text
        }

// Mode tabs: 版本下载 / 模组下载 / 整合包下载.
        // Fixed tab widths (no implicitWidth timing) + a sliding pill so the
        // three buttons never overlap and switching reads as an animation.
        Item {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            height: 34

            Rectangle {
                id: modePill
                x: root.modeIndex * 108
                y: 0
                width: 100
                height: 34
                radius: Theme.shapeFull
                color: Qt.alpha(palette.highlight, 0.15)
                Behavior on x {
                    enabled: Theme.animationsEnabled
                    NumberAnimation { duration: 320; easing.type: Easing.OutBack; easing.overshoot: 1.1 }
                }
            }

            Row {
                anchors.fill: parent
                spacing: 8

                Repeater {
                    model: [
                        { label: "版本下载" },
                        { label: "模组下载" },
                        { label: "整合包下载" }
                    ]
                    delegate: Item {
                        width: 100
                        height: 34

                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.shapeFull
                            color: mtHover.hovered ? Qt.alpha(palette.placeholderText, 0.08) : "transparent"
                        }
                        HoverHandler { id: mtHover }

                        Text {
                            anchors.centerIn: parent
                            text: modelData.label
                            font.pixelSize: 14
                            font.weight: index === root.modeIndex ? Font.Medium : Font.Normal
                            color: index === root.modeIndex ? palette.highlight : palette.placeholderText
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.modeIndex = index
                        }
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: palette.mid; opacity: 0.3 }

Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            // ---------------- 版本下载 ----------------
            Item {
                id: versionPage
                anchors.fill: parent
                visible: opacity > 0
                opacity: root.modeIndex === 0 ? 1 : 0
                x: root.modeIndex === 0 ? 0 : (root.modeIndex < 0 ? -48 : 48)
                Behavior on opacity { enabled: Theme.animationsEnabled; NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
                Behavior on x { enabled: Theme.animationsEnabled; NumberAnimation { duration: 340; easing.type: Easing.OutCubic } }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 16

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Item {
                            Layout.fillWidth: true
                            Layout.leftMargin: 12
                            height: 36

                            // Sliding pill that bounces to the selected category.
                            Rectangle {
                                id: catPill
                                y: 0
                                height: 36
                                radius: Theme.shapeExtraLarge
                                color: Qt.alpha(palette.highlight, 0.15)
                                width: root.currentCategory >= 0 && categoryTabs.count > root.currentCategory
                                       ? categoryTabs.itemAt(root.currentCategory).width
                                       : 0
                                x: root.currentCategory >= 0 && categoryTabs.count > root.currentCategory
                                   ? categoryTabs.itemAt(root.currentCategory).x
                                   : 0
                                Behavior on x {
                                    enabled: Theme.animationsEnabled
                                    NumberAnimation { duration: 320; easing.type: Easing.OutBack; easing.overshoot: 1.1 }
                                }
                                Behavior on width {
                                    enabled: Theme.animationsEnabled
                                    NumberAnimation { duration: 320; easing.type: Easing.OutBack; easing.overshoot: 1.1 }
                                }
                            }

                            Row {
                                anchors.fill: parent
                                spacing: 8

                                Repeater {
                                    id: categoryTabs
                                    delegate: Rectangle {
                                        id: tabBg
                                        implicitWidth: implicitLabel.implicitWidth + 32
                                        width: implicitWidth
                                        height: 36
                                        radius: Theme.shapeExtraLarge
                                        color: "transparent"

                                        Rectangle {
                                            anchors.fill: parent
                                            radius: parent.radius
                                            color: tabHover.hovered ? Qt.alpha(palette.placeholderText, 0.08) : "transparent"
                                            Behavior on color {
                                                enabled: Theme.animationsEnabled
                                                ColorAnimation { duration: 150 }
                                            }
                                        }

                                        Text {
                                            id: implicitLabel
                                            anchors.centerIn: parent
                                            text: modelData.label + " (" + (modelData.versions ? modelData.versions.length : 0) + ")"
                                            font.pixelSize: 13
                                            font.weight: index === root.currentCategory ? Font.Medium : Font.Normal
                                            color: index === root.currentCategory ? palette.highlight : palette.placeholderText
                                        }

                                        HoverHandler { id: tabHover }

                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: root.selectCategory(index)
                                        }
                                    }
                                }
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: palette.mid; opacity: 0.3 }

                    Rectangle {
                        id: loadingContainer
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: "transparent"
                        visible: root.loading

                        BusyIndicator {
                            anchors.centerIn: parent
                            running: parent.visible
                            implicitWidth: 40
                            implicitHeight: 40
                        }
                    }

                    Rectangle {
                        id: listContainer
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: Theme.shapeMedium
                        color: Theme.surfaceContainer
                        clip: true
                        visible: !loading

                        opacity: root.loading ? 0 : 1
                        Behavior on opacity { NumberAnimation { duration: 150 } }

                        ListView {
                            id: versionListView
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 4
                            model: filteredVersions
                            clip: true

                            ScrollBar.vertical: ScrollBar {
                                policy: Theme.alwaysScrollbars ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                                width: 8
                            }

                            add: Transition {
                                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 200 }
                                NumberAnimation { property: "scale"; from: isWin11 ? 0.92 : 1; to: 1; duration: 200 }
                            }
                            populate: Transition {
                                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 200 }
                            }

                            delegate: Rectangle {
                                id: listDelegate
                                width: versionListView.width
                                height: 48
                                radius: Theme.shapeSmall
                                color: mouseArea.containsMouse
                                       ? Qt.alpha(palette.highlight, 0.08)
                                       : "transparent"

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 16
                                    anchors.rightMargin: 16
                                    spacing: 12

                                    Column {
                                        Layout.fillWidth: true
                                        Layout.alignment: Qt.AlignVCenter
                                        spacing: 2

                                        Text {
                                            text: modelData.id || ""
                                            font.pixelSize: 14
                                            font.weight: Font.Medium
                                            color: palette.text
                                            elide: Text.ElideRight
                                        }

                                        Text {
                                            text: modelData.type || ""
                                            font.pixelSize: 11
                                            color: palette.placeholderText
                                        }
                                    }

                                    Text {
                                        text: {
                                            if (modelData.id === kernel.versionManager.latestRelease) return "最新正式版"
                                            if (modelData.id === kernel.versionManager.latestSnapshot) return "最新快照版"
                                            return ""
                                        }
                                        font.pixelSize: 11
                                        color: palette.highlight
                                        visible: text !== ""
                                    }
                                }

                                MouseArea {
                                    id: mouseArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        downloadDialog.versionId = modelData.id
                                        downloadDialog.versionType = modelData.type
                                        downloadDialog.open()
                                    }
                                }
                            }
                        }

                        Text {
                            anchors.centerIn: parent
                            text: "该分类暂无版本"
                            font.pixelSize: 14
                            color: palette.placeholderText
                            visible: !loading && filteredVersions.length === 0
                        }
                    }
                }
            }

            // ---------------- 模组下载 ----------------
            StackView {
                id: modsStack
                anchors.fill: parent
                visible: opacity > 0
                opacity: root.modeIndex === 1 ? 1 : 0
                x: root.modeIndex === 1 ? 0 : (root.modeIndex < 1 ? -48 : 48)
                Behavior on opacity { enabled: Theme.animationsEnabled; NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
                Behavior on x { enabled: Theme.animationsEnabled; NumberAnimation { duration: 340; easing.type: Easing.OutCubic } }
                initialItem: ModsSearchPage {
                    stackView: modsStack
                    downloadDialog: modDownloadDialog
                }
                focus: true

                // All four transitions share one 320ms timing so the entering
                // and exiting pages slide in sync (the covered page's visible
                // sliver keeps fading -> the fade is actually seen), and every
                // page is restored to x=0 / opacity=1 on reveal (no more page
                // stuck invisible-but-clickable after a back navigation).
                pushEnter: Transition {
                    enabled: Theme.animationsEnabled
                    NumberAnimation { property: "x"; to: 0; duration: 320; easing.type: Easing.OutCubic }
                    NumberAnimation { property: "opacity"; to: 1; duration: 160 }
                }
                pushExit: Transition {
                    enabled: Theme.animationsEnabled
                    NumberAnimation { property: "x"; from: 0; to: -item.width; duration: 320; easing.type: Easing.OutCubic }
                    NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 320; easing.type: Easing.InCubic }
                }
                popEnter: Transition {
                    enabled: Theme.animationsEnabled
                    NumberAnimation { property: "x"; to: 0; duration: 320; easing.type: Easing.OutCubic }
                    NumberAnimation { property: "opacity"; to: 1; duration: 320; easing.type: Easing.OutCubic }
                }
                popExit: Transition {
                    enabled: Theme.animationsEnabled
                    NumberAnimation { property: "x"; from: 0; to: item.width; duration: 320; easing.type: Easing.OutCubic }
                    NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 320; easing.type: Easing.InCubic }
                }
            }

            // ---------------- 整合包下载 ----------------
            StackView {
                id: packsStack
                anchors.fill: parent
                visible: opacity > 0
                opacity: root.modeIndex === 2 ? 1 : 0
                x: root.modeIndex === 2 ? 0 : (root.modeIndex < 2 ? -48 : 48)
                Behavior on opacity { enabled: Theme.animationsEnabled; NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
                Behavior on x { enabled: Theme.animationsEnabled; NumberAnimation { duration: 340; easing.type: Easing.OutCubic } }
                initialItem: ModpackSearchPage {
                    stackView: packsStack
                    installDialog: modpackInstallDialog
                }
                focus: true

                pushEnter: Transition {
                    enabled: Theme.animationsEnabled
                    NumberAnimation { property: "x"; to: 0; duration: 320; easing.type: Easing.OutCubic }
                    NumberAnimation { property: "opacity"; to: 1; duration: 160 }
                }
                pushExit: Transition {
                    enabled: Theme.animationsEnabled
                    NumberAnimation { property: "x"; from: 0; to: -item.width; duration: 320; easing.type: Easing.OutCubic }
                    NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 320; easing.type: Easing.InCubic }
                }
                popEnter: Transition {
                    enabled: Theme.animationsEnabled
                    NumberAnimation { property: "x"; to: 0; duration: 320; easing.type: Easing.OutCubic }
                    NumberAnimation { property: "opacity"; to: 1; duration: 320; easing.type: Easing.OutCubic }
                }
                popExit: Transition {
                    enabled: Theme.animationsEnabled
                    NumberAnimation { property: "x"; from: 0; to: item.width; duration: 320; easing.type: Easing.OutCubic }
                    NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 320; easing.type: Easing.InCubic }
                }
            }
        }
    }

DownloadDialog {
        id: downloadDialog
    }

    // Mod / modpack install dialogs are declared here (outside the StackView
    // pages) so they render like DownloadDialog instead of inside a page.
    ModDownloadDialog {
        id: modDownloadDialog
    }

    ModpackInstallDialog {
        id: modpackInstallDialog
    }
}
