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

        // Mode tabs: 版本下载 / 模组下载
        RowLayout {
            Layout.fillWidth: true
            spacing: 4

            Repeater {
                model: [
                    { label: "版本下载" },
                    { label: "模组下载" },
                    { label: "整合包下载" }
                ]
                delegate: Rectangle {
                    id: modeTab
                    property int idx: index
                    width: modeLabel.implicitWidth + 40
                    height: 34
                    radius: Theme.shapeExtraLarge
                    color: idx === root.modeIndex
                           ? Qt.alpha(palette.highlight, 0.15)
                           : (mtHover.hovered ? Qt.alpha(palette.placeholderText, 0.08) : "transparent")
                    HoverHandler { id: mtHover }

                    Text {
                        id: modeLabel
                        anchors.centerIn: parent
                        text: modelData.label
                        font.pixelSize: 14
                        font.weight: idx === root.modeIndex ? Font.Medium : Font.Normal
                        color: idx === root.modeIndex ? palette.highlight : palette.placeholderText
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.modeIndex = idx
                    }
                }
            }

            Item { Layout.fillWidth: true }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: palette.mid; opacity: 0.3 }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.modeIndex

            // ---------------- 版本下载 ----------------
            Item {
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 16

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Repeater {
                            id: categoryTabs
                            delegate: Rectangle {
                                id: tabBg
                                width: implicitLabel.implicitWidth + 32
                                height: 36
                                radius: Theme.shapeExtraLarge
                                color: index === currentCategory
                                       ? Qt.alpha(palette.highlight, 0.15)
                                       : "transparent"

                                Text {
                                    id: implicitLabel
                                    anchors.centerIn: parent
                                    text: modelData.label + " (" + (modelData.versions ? modelData.versions.length : 0) + ")"
                                    font.pixelSize: 13
                                    font.weight: index === currentCategory ? Font.Medium : Font.Normal
                                    color: index === currentCategory ? palette.highlight : palette.placeholderText
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: selectCategory(index)
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
                        color: Qt.alpha(palette.placeholderText, 0.05)
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
                initialItem: ModsSearchPage {
                    stackView: modsStack
                }
                focus: true

                // Slide the covered page out, fading it during the second half
                // of the slide so the transition does not linger fully opaque.
                pushExit: Transition {
                    enabled: Theme.animationsEnabled
                    NumberAnimation { property: "x"; from: 0; to: -item.width; duration: 320; easing.type: Easing.OutCubic }
                    SequentialAnimation {
                        PauseAnimation { duration: 160 }
                        NumberAnimation { property: "opacity"; to: 0; duration: 160; easing.type: Easing.OutCubic }
                    }
                }
                popExit: Transition {
                    enabled: Theme.animationsEnabled
                    NumberAnimation { property: "x"; from: 0; to: item.width; duration: 320; easing.type: Easing.OutCubic }
                    SequentialAnimation {
                        PauseAnimation { duration: 160 }
                        NumberAnimation { property: "opacity"; to: 0; duration: 160; easing.type: Easing.OutCubic }
                    }
                }
            }

            // ---------------- 整合包下载 ----------------
            StackView {
                id: packsStack
                initialItem: ModpackSearchPage {
                    stackView: packsStack
                }
                focus: true

                pushExit: Transition {
                    enabled: Theme.animationsEnabled
                    NumberAnimation { property: "x"; from: 0; to: -item.width; duration: 320; easing.type: Easing.OutCubic }
                    SequentialAnimation {
                        PauseAnimation { duration: 160 }
                        NumberAnimation { property: "opacity"; to: 0; duration: 160; easing.type: Easing.OutCubic }
                    }
                }
                popExit: Transition {
                    enabled: Theme.animationsEnabled
                    NumberAnimation { property: "x"; from: 0; to: item.width; duration: 320; easing.type: Easing.OutCubic }
                    SequentialAnimation {
                        PauseAnimation { duration: 160 }
                        NumberAnimation { property: "opacity"; to: 0; duration: 160; easing.type: Easing.OutCubic }
                    }
                }
            }
        }
    }

    DownloadDialog {
        id: downloadDialog
    }
}
